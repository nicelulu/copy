#include <Interpreters/RequiredSourceColumnsVisitor.h>
#include <Common/typeid_cast.h>
#include <Core/Names.h>
#include <Parsers/IAST.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTSubquery.h>
#include <Parsers/ASTTablesInSelectQuery.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int TYPE_MISMATCH;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}

static std::vector<String> extractNamesFromLambda(const ASTFunction & node)
{
    if (node.arguments->children.size() != 2)
        throw Exception("lambda requires two arguments", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    const auto * lambda_args_tuple = node.arguments->children[0]->As<ASTFunction>();

    if (!lambda_args_tuple || lambda_args_tuple->name != "tuple")
        throw Exception("First argument of lambda must be a tuple", ErrorCodes::TYPE_MISMATCH);

    std::vector<String> names;
    for (auto & child : lambda_args_tuple->arguments->children)
    {
        const auto * identifier = child->As<ASTIdentifier>();
        if (!identifier)
            throw Exception("lambda argument declarations must be identifiers", ErrorCodes::TYPE_MISMATCH);

        names.push_back(identifier->name);
    }

    return names;
}

bool RequiredSourceColumnsMatcher::needChildVisit(ASTPtr & node, const ASTPtr & child)
{
    if (child->As<ASTSelectQuery>())
        return false;

    /// Processed. Do not need children.
    if (node->As<ASTTableExpression>() || node->As<ASTArrayJoin>() || node->As<ASTSelectQuery>())
        return false;

    if (const auto * f = node->As<ASTFunction>())
    {
        /// "indexHint" is a special function for index analysis. Everything that is inside it is not calculated. @sa KeyCondition
        /// "lambda" visit children itself.
        if (f->name == "indexHint" || f->name == "lambda")
            return false;
    }

    return true;
}

void RequiredSourceColumnsMatcher::visit(ASTPtr & ast, Data & data)
{
    /// results are columns

    if (auto * t = ast->As<ASTIdentifier>())
    {
        visit(*t, ast, data);
        return;
    }
    if (auto * t = ast->As<ASTFunction>())
    {
        data.addColumnAliasIfAny(*ast);
        visit(*t, ast, data);
        return;
    }

    /// results are tables

    if (auto * t = ast->As<ASTTablesInSelectQueryElement>())
    {
        visit(*t, ast, data);
        return;
    }

    if (auto * t = ast->As<ASTTableExpression>())
    {
        visit(*t, ast, data);
        return;
    }
    if (auto * t = ast->As<ASTSelectQuery>())
    {
        data.addTableAliasIfAny(*ast);
        visit(*t, ast, data);
        return;
    }
    if (ast->As<ASTSubquery>())
    {
        data.addTableAliasIfAny(*ast);
        return;
    }

    /// other

    if (auto * t = ast->As<ASTArrayJoin>())
    {
        data.has_array_join = true;
        visit(*t, ast, data);
        return;
    }
}

void RequiredSourceColumnsMatcher::visit(ASTSelectQuery & select, const ASTPtr &, Data & data)
{
    /// special case for top-level SELECT items: they are publics
    for (auto & node : select.select_expression_list->children)
    {
        if (const auto * identifier = node->As<ASTIdentifier>())
            data.addColumnIdentifier(*identifier);
        else
            data.addColumnAliasIfAny(*node);
    }

    std::vector<ASTPtr *> out;
    for (auto & node : select.children)
        if (node != select.select_expression_list)
            out.push_back(&node);

    /// revisit select_expression_list (with children) when all the aliases are set
    out.push_back(&select.select_expression_list);

    for (ASTPtr * add_node : out)
        Visitor(data).visit(*add_node);
}

void RequiredSourceColumnsMatcher::visit(const ASTIdentifier & node, const ASTPtr &, Data & data)
{
    if (node.name.empty())
        throw Exception("Expected not empty name", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    if (!data.private_aliases.count(node.name))
        data.addColumnIdentifier(node);
}

void RequiredSourceColumnsMatcher::visit(const ASTFunction & node, const ASTPtr &, Data & data)
{
    /// Do not add formal parameters of the lambda expression
    if (node.name == "lambda")
    {
        Names local_aliases;
        for (const auto & name : extractNamesFromLambda(node))
            if (data.private_aliases.insert(name).second)
                local_aliases.push_back(name);

        /// visit child with masked local aliases
        RequiredSourceColumnsVisitor(data).visit(node.arguments->children[1]);

        for (const auto & name : local_aliases)
            data.private_aliases.erase(name);
    }
}

void RequiredSourceColumnsMatcher::visit(ASTTablesInSelectQueryElement & node, const ASTPtr &, Data & data)
{
    ASTTableExpression * expr = nullptr;
    ASTTableJoin * join = nullptr;

    for (auto & child : node.children)
    {
        if (auto * e = child->As<ASTTableExpression>())
            expr = e;
        if (auto * j = child->As<ASTTableJoin>())
            join = j;
    }

    if (join)
        data.has_table_join = true;
    data.tables.emplace_back(ColumnNamesContext::JoinedTable{expr, join});
}

/// ASTIdentifiers here are tables. Do not visit them as generic ones.
void RequiredSourceColumnsMatcher::visit(ASTTableExpression & node, const ASTPtr &, Data & data)
{
    if (node.database_and_table_name)
        data.addTableAliasIfAny(*node.database_and_table_name);

    if (node.table_function)
        data.addTableAliasIfAny(*node.table_function);

    if (node.subquery)
        data.addTableAliasIfAny(*node.subquery);
}

void RequiredSourceColumnsMatcher::visit(const ASTArrayJoin & node, const ASTPtr &, Data & data)
{
    ASTPtr expression_list = node.expression_list;
    if (!expression_list || expression_list->children.empty())
        throw Exception("Expected not empty expression_list", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    std::vector<ASTPtr *> out;

    /// Tech debt. Ignore ARRAY JOIN top-level identifiers and aliases. There's its own logic for them.
    for (auto & expr : expression_list->children)
    {
        data.addArrayJoinAliasIfAny(*expr);

        if (const auto * identifier = expr->As<ASTIdentifier>())
        {
            data.addArrayJoinIdentifier(*identifier);
            continue;
        }

        out.push_back(&expr);
    }

    for (ASTPtr * add_node : out)
        Visitor(data).visit(*add_node);
}

}
