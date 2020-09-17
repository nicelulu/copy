#include <Interpreters/InterpreterSelectQuery.h>
#include <Interpreters/InterpreterSelectWithUnionQuery.h>
#include <Interpreters/Context.h>

#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTSubquery.h>
#include <Parsers/ASTTablesInSelectQuery.h>
#include <Parsers/ASTSelectWithUnionQuery.h>

#include <Storages/StorageView.h>
#include <Storages/StorageFactory.h>
#include <Storages/SelectQueryDescription.h>

#include <Common/typeid_cast.h>

#include <Processors/Pipe.h>
#include <Processors/Transforms/MaterializingTransform.h>
#include <Processors/Transforms/ConvertingTransform.h>
#include <Processors/QueryPlan/MaterializingStep.h>
#include <Processors/QueryPlan/ConvertingStep.h>
#include <Processors/QueryPlan/SettingQuotaAndLimitsStep.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_QUERY;
    extern const int LOGICAL_ERROR;
}


StorageView::StorageView(
    const StorageID & table_id_,
    const ASTCreateQuery & query,
    const ColumnsDescription & columns_)
    : IStorage(table_id_)
{
    StorageInMemoryMetadata storage_metadata;
    storage_metadata.setColumns(columns_);

    if (!query.select)
        throw Exception("SELECT query is not specified for " + getName(), ErrorCodes::INCORRECT_QUERY);

    SelectQueryDescription description;

    description.inner_query = query.select->ptr();
    storage_metadata.setSelectQuery(description);
    setInMemoryMetadata(storage_metadata);
}


Pipe StorageView::read(
    const Names & column_names,
    const StorageMetadataPtr & metadata_snapshot,
    const SelectQueryInfo & query_info,
    const Context & context,
    QueryProcessingStage::Enum /*processed_stage*/,
    const size_t /*max_block_size*/,
    const unsigned /*num_streams*/)
{
    Pipes pipes;

    ASTPtr current_inner_query = metadata_snapshot->getSelectQuery().inner_query;

    if (query_info.view_query)
    {
        if (!query_info.view_query->as<ASTSelectWithUnionQuery>())
            throw Exception("Unexpected optimized VIEW query", ErrorCodes::LOGICAL_ERROR);
        current_inner_query = query_info.view_query->clone();
    }

    InterpreterSelectWithUnionQuery interpreter(current_inner_query, context, {}, column_names);

    auto pipeline = interpreter.execute().pipeline;

    /// It's expected that the columns read from storage are not constant.
    /// Because method 'getSampleBlockForColumns' is used to obtain a structure of result in InterpreterSelectQuery.
    pipeline.addSimpleTransform([](const Block & header)
    {
        return std::make_shared<MaterializingTransform>(header);
    });

    /// And also convert to expected structure.
    pipeline.addSimpleTransform([&](const Block & header)
    {
        return std::make_shared<ConvertingTransform>(
            header, metadata_snapshot->getSampleBlockForColumns(
                column_names, getVirtuals(), getStorageID()), ConvertingTransform::MatchColumnsMode::Name);
    });

    return QueryPipeline::getPipe(std::move(pipeline));
}

void StorageView::read(
        QueryPlan & query_plan,
        TableLockHolder table_lock,
        StorageMetadataPtr metadata_snapshot,
        StreamLocalLimits & limits,
        std::shared_ptr<const EnabledQuota> quota,
        const Names & column_names,
        const SelectQueryInfo & query_info,
        std::shared_ptr<Context> context,
        QueryProcessingStage::Enum /*processed_stage*/,
        const size_t /*max_block_size*/,
        const unsigned /*num_streams*/)
{
    ASTPtr current_inner_query = metadata_snapshot->getSelectQuery().inner_query;

    if (query_info.view_query)
    {
        if (!query_info.view_query->as<ASTSelectWithUnionQuery>())
            throw Exception("Unexpected optimized VIEW query", ErrorCodes::LOGICAL_ERROR);
        current_inner_query = query_info.view_query->clone();
    }

    InterpreterSelectWithUnionQuery interpreter(current_inner_query, *context, {}, column_names);
    interpreter.buildQueryPlan(query_plan);

    /// It's expected that the columns read from storage are not constant.
    /// Because method 'getSampleBlockForColumns' is used to obtain a structure of result in InterpreterSelectQuery.
    auto materializing = std::make_unique<MaterializingStep>(query_plan.getCurrentDataStream());
    materializing->setStepDescription("Materialize constants after VIEW subquery");
    query_plan.addStep(std::move(materializing));

    /// And also convert to expected structure.
    auto header = metadata_snapshot->getSampleBlockForColumns(column_names, getVirtuals(), getStorageID());
    auto converting = std::make_unique<ConvertingStep>(query_plan.getCurrentDataStream(), header);
    converting->setStepDescription("Convert VIEW subquery result to VIEW table structure");
    query_plan.addStep(std::move(converting));

    /// Extend lifetime of context, table lock, storage. Set limits and quota.
    auto adding_limits_and_quota = std::make_unique<SettingQuotaAndLimitsStep>(
            query_plan.getCurrentDataStream(),
            shared_from_this(),
            std::move(table_lock),
            limits,
            std::move(quota),
            std::move(context));
    adding_limits_and_quota->setStepDescription("Set limits and quota for VIEW subquery");
    query_plan.addStep(std::move(adding_limits_and_quota));
}

static ASTTableExpression * getFirstTableExpression(ASTSelectQuery & select_query)
{
    auto * select_element = select_query.tables()->children[0]->as<ASTTablesInSelectQueryElement>();

    if (!select_element->table_expression)
        throw Exception("Logical error: incorrect table expression", ErrorCodes::LOGICAL_ERROR);

    return select_element->table_expression->as<ASTTableExpression>();
}

void StorageView::replaceWithSubquery(ASTSelectQuery & outer_query, ASTPtr view_query, ASTPtr & view_name)
{
    ASTTableExpression * table_expression = getFirstTableExpression(outer_query);

    if (!table_expression->database_and_table_name)
    {
        // If it's a view table function, add a fake db.table name.
        if (table_expression->table_function && table_expression->table_function->as<ASTFunction>()->name == "view")
            table_expression->database_and_table_name = std::make_shared<ASTIdentifier>("__view");
        else
            throw Exception("Logical error: incorrect table expression", ErrorCodes::LOGICAL_ERROR);
    }

    DatabaseAndTableWithAlias db_table(table_expression->database_and_table_name);
    String alias = db_table.alias.empty() ? db_table.table : db_table.alias;

    view_name = table_expression->database_and_table_name;
    table_expression->database_and_table_name = {};
    table_expression->subquery = std::make_shared<ASTSubquery>();
    table_expression->subquery->children.push_back(view_query);
    table_expression->subquery->setAlias(alias);

    for (auto & child : table_expression->children)
        if (child.get() == view_name.get())
            child = view_query;
}

ASTPtr StorageView::restoreViewName(ASTSelectQuery & select_query, const ASTPtr & view_name)
{
    ASTTableExpression * table_expression = getFirstTableExpression(select_query);

    if (!table_expression->subquery)
        throw Exception("Logical error: incorrect table expression", ErrorCodes::LOGICAL_ERROR);

    ASTPtr subquery = table_expression->subquery;
    table_expression->subquery = {};
    table_expression->database_and_table_name = view_name;

    for (auto & child : table_expression->children)
        if (child.get() == subquery.get())
            child = view_name;
    return subquery->children[0];
}

void registerStorageView(StorageFactory & factory)
{
    factory.registerStorage("View", [](const StorageFactory::Arguments & args)
    {
        if (args.query.storage)
            throw Exception("Specifying ENGINE is not allowed for a View", ErrorCodes::INCORRECT_QUERY);

        return StorageView::create(args.table_id, args.query, args.columns);
    });
}

}
