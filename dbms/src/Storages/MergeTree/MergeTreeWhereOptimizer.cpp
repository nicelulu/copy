#include <Storages/MergeTree/MergeTreeWhereOptimizer.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Storages/MergeTree/KeyCondition.h>
#include <Interpreters/IdentifierSemantic.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTSubquery.h>
#include <Parsers/formatAST.h>
#include <Interpreters/misc.h>
#include <Common/typeid_cast.h>
#include <DataTypes/NestedUtils.h>
#include <ext/map.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

/// Conditions like "x = N" are considered good if abs(N) > threshold.
/// This is used to assume that condition is likely to have good selectivity.
static constexpr auto threshold = 2;


MergeTreeWhereOptimizer::MergeTreeWhereOptimizer(
    SelectQueryInfo & query_info,
    const Context & context,
    const MergeTreeData & data,
    const Names & queried_columns_,
    Logger * log_)
        : table_columns{ext::map<std::unordered_set>(data.getColumns().getAllPhysical(),
            [] (const NameAndTypePair & col) { return col.name; })},
        queried_columns{queried_columns_},
        block_with_constants{KeyCondition::getBlockWithConstants(query_info.query, query_info.syntax_analyzer_result, context)},
        log{log_}
{
    if (!data.primary_key_columns.empty())
        first_primary_key_column = data.primary_key_columns[0];

    calculateColumnSizes(data, queried_columns);
    determineArrayJoinedNames(query_info.query->as<ASTSelectQuery &>());
    optimize(query_info.query->as<ASTSelectQuery &>());
}


void MergeTreeWhereOptimizer::calculateColumnSizes(const MergeTreeData & data, const Names & column_names)
{
    for (const auto & column_name : column_names)
    {
        UInt64 size = data.getColumnCompressedSize(column_name);
        column_sizes[column_name] = size;
        total_size_of_queried_columns += size;
    }
}


static void collectIdentifiersNoSubqueries(const ASTPtr & ast, NameSet & set)
{
    if (auto opt_name = tryGetIdentifierName(ast))
        return (void)set.insert(*opt_name);

    if (ast->as<ASTSubquery>())
        return;

    for (const auto & child : ast->children)
        collectIdentifiersNoSubqueries(child, set);
}

void MergeTreeWhereOptimizer::analyzeImpl(Conditions & res, const ASTPtr & node) const
{
    if (const auto * func_and = node->as<ASTFunction>(); func_and && func_and->name == "and")
    {
        for (const auto & elem : func_and->arguments->children)
            analyzeImpl(res, elem);
    }
    else
    {
        Condition cond;
        cond.node = node;

        collectIdentifiersNoSubqueries(node, cond.identifiers);

        cond.viable =
            /// Condition depend on some column. Constant expressions are not moved.
            !cond.identifiers.empty()
            && !cannotBeMoved(node)
            /// Do not take into consideration the conditions consisting only of the first primary key column
            && !hasPrimaryKeyAtoms(node)
            /// Only table columns are considered. Not array joined columns. NOTE We're assuming that aliases was expanded.
            && isSubsetOfTableColumns(cond.identifiers)
            /// Do not move conditions involving all queried columns.
            && cond.identifiers.size() < queried_columns.size();

        if (cond.viable)
        {
            cond.columns_size = getIdentifiersColumnSize(cond.identifiers);
            cond.good = isConditionGood(node);
        }

        res.emplace_back(std::move(cond));
    }
}

/// Transform conjunctions chain in WHERE expression to Conditions list.
MergeTreeWhereOptimizer::Conditions MergeTreeWhereOptimizer::analyze(const ASTPtr & expression) const
{
    Conditions res;
    analyzeImpl(res, expression);
    return res;
}

/// Transform Conditions list to WHERE or PREWHERE expression.
ASTPtr MergeTreeWhereOptimizer::reconstruct(const Conditions & conditions) const
{
    if (conditions.empty())
        return {};

    if (conditions.size() == 1)
        return conditions.front().node;

    const auto function = std::make_shared<ASTFunction>();

    function->name = "and";
    function->arguments = std::make_shared<ASTExpressionList>();
    function->children.push_back(function->arguments);

    for (const auto & elem : conditions)
        function->arguments->children.push_back(elem.node);

    return function;
}


void MergeTreeWhereOptimizer::optimize(ASTSelectQuery & select) const
{
    if (!select.where() || select.prewhere())
        return;

    Conditions where_conditions = analyze(select.where());
    Conditions prewhere_conditions;

    UInt64 total_size_of_moved_conditions = 0;

    /// Move condition and all other conditions depend on the same set of columns.
    auto move_condition = [&](Conditions::iterator cond_it)
    {
        prewhere_conditions.splice(prewhere_conditions.end(), where_conditions, cond_it);
        total_size_of_moved_conditions += cond_it->columns_size;

        /// Move all other conditions that depend on the same set of columns.
        for (auto jt = where_conditions.begin(); jt != where_conditions.end();)
        {
            if (jt->columns_size == cond_it->columns_size && jt->identifiers == cond_it->identifiers)
                prewhere_conditions.splice(prewhere_conditions.end(), where_conditions, jt++);
            else
                ++jt;
        }
    };

    /// Move conditions unless the ratio of total_size_of_moved_conditions to the total_size_of_queried_columns is less than some threshold.
    while (!where_conditions.empty())
    {
        /// Move the best condition to PREWHERE if it is viable.

        auto it = std::min_element(where_conditions.begin(), where_conditions.end());

        if (!it->viable)
            break;

        /// 10% ratio is just a guess.
        if (total_size_of_moved_conditions > 0 && (total_size_of_moved_conditions + it->columns_size) * 10 > total_size_of_queried_columns)
            break;

        move_condition(it);
    }

    /// Nothing was moved.
    if (prewhere_conditions.empty())
        return;

    /// Rewrite the SELECT query.

    select.setExpression(ASTSelectQuery::Expression::WHERE, reconstruct(where_conditions));
    select.setExpression(ASTSelectQuery::Expression::PREWHERE, reconstruct(prewhere_conditions));

    LOG_DEBUG(log, "MergeTreeWhereOptimizer: condition \"" << select.prewhere() << "\" moved to PREWHERE");
}


UInt64 MergeTreeWhereOptimizer::getIdentifiersColumnSize(const NameSet & identifiers) const
{
    UInt64 size = 0;

    for (const auto & identifier : identifiers)
        if (column_sizes.count(identifier))
            size += column_sizes.at(identifier);

    return size;
}


bool MergeTreeWhereOptimizer::isConditionGood(const ASTPtr & condition) const
{
    const auto * function = condition->as<ASTFunction>();
    if (!function)
        return false;

    /** we are only considering conditions of form `equals(one, another)` or `one = another`,
        * especially if either `one` or `another` is ASTIdentifier */
    if (function->name != "equals")
        return false;

    auto left_arg = function->arguments->children.front().get();
    auto right_arg = function->arguments->children.back().get();

    /// try to ensure left_arg points to ASTIdentifier
    if (!left_arg->as<ASTIdentifier>() && right_arg->as<ASTIdentifier>())
        std::swap(left_arg, right_arg);

    if (left_arg->as<ASTIdentifier>())
    {
        /// condition may be "good" if only right_arg is a constant and its value is outside the threshold
        if (const auto * literal = right_arg->as<ASTLiteral>())
        {
            const auto & field = literal->value;
            const auto type = field.getType();

            /// check the value with respect to threshold
            if (type == Field::Types::UInt64)
            {
                const auto value = field.get<UInt64>();
                return value > threshold;
            }
            else if (type == Field::Types::Int64)
            {
                const auto value = field.get<Int64>();
                return value < -threshold || threshold < value;
            }
            else if (type == Field::Types::Float64)
            {
                const auto value = field.get<Float64>();
                return value < threshold || threshold < value;
            }
        }
    }

    return false;
}


bool MergeTreeWhereOptimizer::hasPrimaryKeyAtoms(const ASTPtr & ast) const
{
    if (const auto * func = ast->as<ASTFunction>())
    {
        const auto & args = func->arguments->children;

        if ((func->name == "not" && 1 == args.size()) || func->name == "and" || func->name == "or")
        {
            for (const auto & arg : args)
                if (hasPrimaryKeyAtoms(arg))
                    return true;

            return false;
        }
    }

    return isPrimaryKeyAtom(ast);
}


bool MergeTreeWhereOptimizer::isPrimaryKeyAtom(const ASTPtr & ast) const
{
    if (const auto * func = ast->as<ASTFunction>())
    {
        if (!KeyCondition::atom_map.count(func->name))
            return false;

        const auto & args = func->arguments->children;
        if (args.size() != 2)
            return false;

        const auto & first_arg_name = args.front()->getColumnName();
        const auto & second_arg_name = args.back()->getColumnName();

        if ((first_primary_key_column == first_arg_name && isConstant(args[1]))
            || (first_primary_key_column == second_arg_name && isConstant(args[0]))
            || (first_primary_key_column == first_arg_name && functionIsInOrGlobalInOperator(func->name)))
            return true;
    }

    return false;
}


bool MergeTreeWhereOptimizer::isConstant(const ASTPtr & expr) const
{
    const auto column_name = expr->getColumnName();

    if (expr->as<ASTLiteral>()
        || (block_with_constants.has(column_name) && isColumnConst(*block_with_constants.getByName(column_name).column)))
        return true;

    return false;
}


bool MergeTreeWhereOptimizer::isSubsetOfTableColumns(const NameSet & identifiers) const
{
    for (const auto & identifier : identifiers)
        if (table_columns.count(identifier) == 0)
            return false;

    return true;
}


bool MergeTreeWhereOptimizer::cannotBeMoved(const ASTPtr & ptr) const
{
    if (const auto * function_ptr = ptr->as<ASTFunction>())
    {
        /// disallow arrayJoin expressions to be moved to PREWHERE for now
        if ("arrayJoin" == function_ptr->name)
            return true;

        /// disallow GLOBAL IN, GLOBAL NOT IN
        if ("globalIn" == function_ptr->name
            || "globalNotIn" == function_ptr->name)
            return true;

        /// indexHint is a special function that it does not make sense to transfer to PREWHERE
        if ("indexHint" == function_ptr->name)
            return true;
    }
    else if (auto opt_name = IdentifierSemantic::getColumnName(ptr))
    {
        /// disallow moving result of ARRAY JOIN to PREWHERE
        if (array_joined_names.count(*opt_name) ||
            array_joined_names.count(Nested::extractTableName(*opt_name)))
            return true;
    }

    for (const auto & child : ptr->children)
        if (cannotBeMoved(child))
            return true;

    return false;
}


void MergeTreeWhereOptimizer::determineArrayJoinedNames(ASTSelectQuery & select)
{
    auto array_join_expression_list = select.array_join_expression_list();

    /// much simplified code from ExpressionAnalyzer::getArrayJoinedColumns()
    if (!array_join_expression_list)
        return;

    for (const auto & ast : array_join_expression_list->children)
        array_joined_names.emplace(ast->getAliasOrColumnName());
}

}
