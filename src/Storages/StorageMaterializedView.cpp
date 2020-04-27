#include <Storages/StorageMaterializedView.h>

#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTSelectWithUnionQuery.h>
#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTDropQuery.h>

#include <Interpreters/Context.h>
#include <Interpreters/InterpreterCreateQuery.h>
#include <Interpreters/InterpreterDropQuery.h>
#include <Interpreters/InterpreterRenameQuery.h>
#include <Interpreters/getTableExpressions.h>
#include <Interpreters/AddDefaultDatabaseVisitor.h>
#include <Access/AccessFlags.h>
#include <DataStreams/IBlockInputStream.h>
#include <DataStreams/IBlockOutputStream.h>

#include <Storages/AlterCommands.h>
#include <Storages/StorageFactory.h>
#include <Storages/ReadInOrderOptimizer.h>

#include <Common/typeid_cast.h>
#include <Processors/Sources/SourceFromInputStream.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
    extern const int LOGICAL_ERROR;
    extern const int INCORRECT_QUERY;
    extern const int QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW;
}

static inline String generateInnerTableName(const StorageID & view_id)
{
    if (view_id.hasUUID())
        return ".inner_id." + toString(view_id.uuid);
    return ".inner." + view_id.getTableName();
}

static StorageID extractDependentTableFromSelectQuery(ASTSelectQuery & query, const Context & context, bool add_default_db = true)
{
    if (add_default_db)
    {
        AddDefaultDatabaseVisitor visitor(context.getCurrentDatabase(), nullptr);
        visitor.visit(query);
    }

    if (auto db_and_table = getDatabaseAndTable(query, 0))
    {
        return StorageID(db_and_table->database, db_and_table->table/*, db_and_table->uuid*/);
    }
    else if (auto subquery = extractTableExpression(query, 0))
    {
        auto * ast_select = subquery->as<ASTSelectWithUnionQuery>();
        if (!ast_select)
            throw Exception("Logical error while creating StorageMaterializedView. "
                            "Could not retrieve table name from select query.",
                            DB::ErrorCodes::LOGICAL_ERROR);
        if (ast_select->list_of_selects->children.size() != 1)
            throw Exception("UNION is not supported for MATERIALIZED VIEW",
                  ErrorCodes::QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW);

        auto & inner_query = ast_select->list_of_selects->children.at(0);

        return extractDependentTableFromSelectQuery(inner_query->as<ASTSelectQuery &>(), context, false);
    }
    else
        return StorageID::createEmpty();
}


static void checkAllowedQueries(const ASTSelectQuery & query)
{
    if (query.prewhere() || query.final() || query.sampleSize())
        throw Exception("MATERIALIZED VIEW cannot have PREWHERE, SAMPLE or FINAL.", DB::ErrorCodes::QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW);

    ASTPtr subquery = extractTableExpression(query, 0);
    if (!subquery)
        return;

    if (const auto * ast_select = subquery->as<ASTSelectWithUnionQuery>())
    {
        if (ast_select->list_of_selects->children.size() != 1)
            throw Exception("UNION is not supported for MATERIALIZED VIEW", ErrorCodes::QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW);

        const auto & inner_query = ast_select->list_of_selects->children.at(0);

        checkAllowedQueries(inner_query->as<ASTSelectQuery &>());
    }
}


StorageMaterializedView::StorageMaterializedView(
    const StorageID & table_id_,
    Context & local_context,
    const ASTCreateQuery & query,
    const ColumnsDescription & columns_,
    bool attach_)
    : IStorage(table_id_), global_context(local_context.getGlobalContext())
{
    setColumns(columns_);

    if (!query.select)
        throw Exception("SELECT query is not specified for " + getName(), ErrorCodes::INCORRECT_QUERY);

    /// If the destination table is not set, use inner table
    has_inner_table = query.to_table_id.empty();
    if (has_inner_table && !query.storage)
        throw Exception(
            "You must specify where to save results of a MaterializedView query: either ENGINE or an existing table in a TO clause",
            ErrorCodes::INCORRECT_QUERY);

    if (query.select->list_of_selects->children.size() != 1)
        throw Exception("UNION is not supported for MATERIALIZED VIEW", ErrorCodes::QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW);

    select = query.select->clone();
    inner_query = query.select->list_of_selects->children.at(0);

    auto & select_query = inner_query->as<ASTSelectQuery &>();
    checkAllowedQueries(select_query);

    select_table_id = extractDependentTableFromSelectQuery(select_query, local_context);

    if (!has_inner_table)
        target_table_id = query.to_table_id;
    else if (attach_)
    {
        /// If there is an ATTACH request, then the internal table must already be created.
        target_table_id = StorageID(getStorageID().database_name, generateInnerTableName(getStorageID()));
    }
    else
    {
        /// We will create a query to create an internal table.
        auto manual_create_query = std::make_shared<ASTCreateQuery>();
        manual_create_query->database = getStorageID().database_name;
        manual_create_query->table = generateInnerTableName(getStorageID());

        auto new_columns_list = std::make_shared<ASTColumns>();
        new_columns_list->set(new_columns_list->columns, query.columns_list->columns->ptr());

        manual_create_query->set(manual_create_query->columns_list, new_columns_list);
        manual_create_query->set(manual_create_query->storage, query.storage->ptr());

        InterpreterCreateQuery create_interpreter(manual_create_query, local_context);
        create_interpreter.setInternal(true);
        create_interpreter.execute();

        target_table_id = DatabaseCatalog::instance().getTable({manual_create_query->database, manual_create_query->table})->getStorageID();
    }

    if (!select_table_id.empty())
        DatabaseCatalog::instance().addDependency(select_table_id, getStorageID());
}

StorageInMemoryMetadata StorageMaterializedView::getInMemoryMetadata() const
{
    StorageInMemoryMetadata result(getColumns(), getIndices(), getConstraints());
    result.select = getSelectQuery();
    return result;
}

QueryProcessingStage::Enum StorageMaterializedView::getQueryProcessingStage(const Context &context, QueryProcessingStage::Enum to_stage, const ASTPtr &query_ptr) const
{
    return getTargetTable()->getQueryProcessingStage(context, to_stage, query_ptr);
}

Pipes StorageMaterializedView::read(
    const Names & column_names,
    const SelectQueryInfo & query_info,
    const Context & context,
    QueryProcessingStage::Enum processed_stage,
    const size_t max_block_size,
    const unsigned num_streams)
{
    auto storage = getTargetTable();
    auto lock = storage->lockStructureForShare(
            false, context.getCurrentQueryId(), context.getSettingsRef().lock_acquire_timeout);

    if (query_info.order_by_optimizer)
        query_info.input_sorting_info = query_info.order_by_optimizer->getInputOrder(storage);

    Pipes pipes = storage->read(column_names, query_info, context, processed_stage, max_block_size, num_streams);

    for (auto & pipe : pipes)
        pipe.addTableLock(lock);

    return pipes;
}

BlockOutputStreamPtr StorageMaterializedView::write(const ASTPtr & query, const Context & context)
{
    auto storage = getTargetTable();
    auto lock = storage->lockStructureForShare(
            true, context.getCurrentQueryId(), context.getSettingsRef().lock_acquire_timeout);
    auto stream = storage->write(query, context);
    stream->addTableLock(lock);
    return stream;
}


static void executeDropQuery(ASTDropQuery::Kind kind, Context & global_context, const StorageID & target_table_id)
{
    if (DatabaseCatalog::instance().tryGetTable(target_table_id))
    {
        /// We create and execute `drop` query for internal table.
        auto drop_query = std::make_shared<ASTDropQuery>();
        drop_query->database = target_table_id.database_name;
        drop_query->table = target_table_id.table_name;
        drop_query->kind = kind;
        drop_query->no_delay = true;
        ASTPtr ast_drop_query = drop_query;
        InterpreterDropQuery drop_interpreter(ast_drop_query, global_context);
        drop_interpreter.execute();
    }
}


void StorageMaterializedView::drop()
{
    auto table_id = getStorageID();
    if (!select_table_id.empty())
        DatabaseCatalog::instance().removeDependency(select_table_id, table_id);

    if (has_inner_table && tryGetTargetTable())
        executeDropQuery(ASTDropQuery::Kind::Drop, global_context, target_table_id);
}

void StorageMaterializedView::truncate(const ASTPtr &, const Context &, TableStructureWriteLockHolder &)
{
    if (has_inner_table)
        executeDropQuery(ASTDropQuery::Kind::Truncate, global_context, target_table_id);
}

void StorageMaterializedView::checkStatementCanBeForwarded() const
{
    if (!has_inner_table)
        throw Exception(
            "MATERIALIZED VIEW targets existing table " + target_table_id.getNameForLogs() + ". "
            + "Execute the statement directly on it.", ErrorCodes::INCORRECT_QUERY);
}

bool StorageMaterializedView::optimize(const ASTPtr & query, const ASTPtr & partition, bool final, bool deduplicate, const Context & context)
{
    checkStatementCanBeForwarded();
    return getTargetTable()->optimize(query, partition, final, deduplicate, context);
}

void StorageMaterializedView::alter(
    const AlterCommands & params,
    const Context & context,
    TableStructureWriteLockHolder & table_lock_holder)
{
    lockStructureExclusively(table_lock_holder, context.getCurrentQueryId(), context.getSettingsRef().lock_acquire_timeout);
    auto table_id = getStorageID();
    StorageInMemoryMetadata metadata = getInMemoryMetadata();
    params.apply(metadata);

    /// start modify query
    if (context.getSettingsRef().allow_experimental_alter_materialized_view_structure)
    {
        auto & new_select = metadata.select->as<ASTSelectWithUnionQuery &>();

        if (new_select.list_of_selects->children.size() != 1)
            throw Exception("UNION is not supported for MATERIALIZED VIEW", ErrorCodes::QUERY_IS_NOT_SUPPORTED_IN_MATERIALIZED_VIEW);

        auto & new_inner_query = new_select.list_of_selects->children.at(0);
        auto & select_query = new_inner_query->as<ASTSelectQuery &>();
        checkAllowedQueries(select_query);

        auto new_select_table_id = extractDependentTableFromSelectQuery(select_query, context);
        DatabaseCatalog::instance().updateDependency(select_table_id, table_id, new_select_table_id, table_id);

        select_table_id = new_select_table_id;
        select = metadata.select;
        inner_query = new_inner_query;
    }
    /// end modify query

    DatabaseCatalog::instance().getDatabase(table_id.database_name)->alterTable(context, table_id, metadata);
    setColumns(std::move(metadata.columns));
}


void StorageMaterializedView::checkAlterIsPossible(const AlterCommands & commands, const Settings & settings)
{
    if (settings.allow_experimental_alter_materialized_view_structure)
    {
        for (const auto & command : commands)
        {
            if (!command.isCommentAlter() && command.type != AlterCommand::MODIFY_QUERY)
                throw Exception(
                    "Alter of type '" + alterTypeToString(command.type) + "' is not supported by storage " + getName(),
                    ErrorCodes::NOT_IMPLEMENTED);
        }
    }
    else
    {
        for (const auto & command : commands)
        {
            if (!command.isCommentAlter())
                throw Exception(
                    "Alter of type '" + alterTypeToString(command.type) + "' is not supported by storage " + getName(),
                    ErrorCodes::NOT_IMPLEMENTED);
        }
    }
}

void StorageMaterializedView::alterPartition(const ASTPtr & query, const PartitionCommands &commands, const Context &context)
{
    checkStatementCanBeForwarded();
    getTargetTable()->alterPartition(query, commands, context);
}

void StorageMaterializedView::mutate(const MutationCommands & commands, const Context & context)
{
    checkStatementCanBeForwarded();
    getTargetTable()->mutate(commands, context);
}

void StorageMaterializedView::renameInMemory(const StorageID & new_table_id)
{
    auto old_table_id = getStorageID();
    bool from_atomic_to_atomic_database = old_table_id.hasUUID() && new_table_id.hasUUID();

    if (has_inner_table && tryGetTargetTable() && !from_atomic_to_atomic_database)
    {
        auto new_target_table_name = generateInnerTableName(new_table_id);
        auto rename = std::make_shared<ASTRenameQuery>();

        ASTRenameQuery::Table from;
        from.database = target_table_id.database_name;
        from.table = target_table_id.table_name;

        ASTRenameQuery::Table to;
        to.database = target_table_id.database_name;
        to.table = new_target_table_name;

        ASTRenameQuery::Element elem;
        elem.from = from;
        elem.to = to;
        rename->elements.emplace_back(elem);

        InterpreterRenameQuery(rename, global_context).execute();
        target_table_id.table_name = new_target_table_name;
    }

    IStorage::renameInMemory(new_table_id);
    // TODO Actually we don't need to update dependency if MV has UUID, but then db and table name will be outdated
    DatabaseCatalog::instance().updateDependency(select_table_id, old_table_id, select_table_id, getStorageID());
}

void StorageMaterializedView::shutdown()
{
    /// Make sure the dependency is removed after DETACH TABLE
    if (!select_table_id.empty())
        DatabaseCatalog::instance().removeDependency(select_table_id, getStorageID());
}

StoragePtr StorageMaterializedView::getTargetTable() const
{
    return DatabaseCatalog::instance().getTable(target_table_id);
}

StoragePtr StorageMaterializedView::tryGetTargetTable() const
{
    return DatabaseCatalog::instance().tryGetTable(target_table_id);
}

Strings StorageMaterializedView::getDataPaths() const
{
    if (auto table = tryGetTargetTable())
        return table->getDataPaths();
    return {};
}

void StorageMaterializedView::checkTableCanBeDropped() const
{
    /// Don't drop the target table if it was created manually via 'TO inner_table' statement
    if (!has_inner_table)
        return;

    auto target_table = tryGetTargetTable();
    if (!target_table)
        return;

    target_table->checkTableCanBeDropped();
}

void StorageMaterializedView::checkPartitionCanBeDropped(const ASTPtr & partition)
{
    /// Don't drop the partition in target table if it was created manually via 'TO inner_table' statement
    if (!has_inner_table)
        return;

    auto target_table = tryGetTargetTable();
    if (!target_table)
        return;

    target_table->checkPartitionCanBeDropped(partition);
}

ActionLock StorageMaterializedView::getActionLock(StorageActionBlockType type)
{
    return has_inner_table ? getTargetTable()->getActionLock(type) : ActionLock{};
}

void registerStorageMaterializedView(StorageFactory & factory)
{
    factory.registerStorage("MaterializedView", [](const StorageFactory::Arguments & args)
    {
        /// Pass local_context here to convey setting for inner table
        return StorageMaterializedView::create(
            args.table_id, args.local_context, args.query,
            args.columns, args.attach);
    });
}

}
