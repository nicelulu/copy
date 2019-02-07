#include <Common/typeid_cast.h>

#include <Interpreters/IdentifierSemantic.h>

namespace DB
{

std::optional<String> IdentifierSemantic::getColumnName(const ASTIdentifier & node)
{
    if (!node.semantic->special)
        return node.name;
    return {};
}

std::optional<String> IdentifierSemantic::getColumnName(const ASTPtr & ast)
{
    if (ast)
        if (auto id = typeid_cast<const ASTIdentifier *>(ast.get()))
            if (!id->semantic->special)
                return id->name;
    return {};
}

std::optional<String> IdentifierSemantic::getTableName(const ASTIdentifier & node)
{
    if (node.semantic->special)
        return node.name;
    return {};
}

std::optional<String> IdentifierSemantic::getTableName(const ASTPtr & ast)
{
    if (ast)
        if (auto id = typeid_cast<const ASTIdentifier *>(ast.get()))
            if (id->semantic->special)
                return id->name;
    return {};
}


void IdentifierSemantic::setNeedLongName(ASTIdentifier & identifier, bool value)
{
    identifier.semantic->need_long_name = value;
}

std::pair<String, String> IdentifierSemantic::extractDatabaseAndTable(const ASTIdentifier & identifier)
{
    if (identifier.name_parts.size() > 2)
        throw Exception("Logical error: more than two components in table expression", ErrorCodes::LOGICAL_ERROR);

    if (identifier.name_parts.size() == 2)
        return { identifier.name_parts[0], identifier.name_parts[1] };
    return { "", identifier.name };
}

bool IdentifierSemantic::doesIdentifierBelongTo(const ASTIdentifier & identifier, const String & database, const String & table)
{
    size_t num_components = identifier.name_parts.size();
    if (num_components >= 3)
        return identifier.name_parts[0] == database &&
               identifier.name_parts[1] == table;
    return false;
}

bool IdentifierSemantic::doesIdentifierBelongTo(const ASTIdentifier & identifier, const String & table)
{
    size_t num_components = identifier.name_parts.size();
    if (num_components >= 2)
        return identifier.name_parts[0] == table;
    return false;
}

size_t IdentifierSemantic::canReferColumnToTable(const ASTIdentifier & identifier, const DatabaseAndTableWithAlias & db_and_table)
{
    /// database.table.column
    if (doesIdentifierBelongTo(identifier, db_and_table.database, db_and_table.table))
        return 2;

    /// table.column or alias.column.
    if (doesIdentifierBelongTo(identifier, db_and_table.table) ||
        doesIdentifierBelongTo(identifier, db_and_table.alias))
        return 1;

    return 0;
}

/// Checks that ast is ASTIdentifier and remove num_qualifiers_to_strip components from left.
/// Example: 'database.table.name' -> (num_qualifiers_to_strip = 2) -> 'name'.
void IdentifierSemantic::setColumnShortName(ASTIdentifier & identifier, size_t to_strip)
{
    if (!to_strip)
        return;

    std::vector<String> stripped(identifier.name_parts.begin() + to_strip, identifier.name_parts.end());

    DB::String new_name;
    for (const auto & part : stripped)
    {
        if (!new_name.empty())
            new_name += '.';
        new_name += part;
    }
    identifier.name.swap(new_name);
}

void IdentifierSemantic::setColumnNormalName(ASTIdentifier & identifier, const DatabaseAndTableWithAlias & db_and_table)
{
    size_t match = IdentifierSemantic::canReferColumnToTable(identifier, db_and_table);

    setColumnShortName(identifier, match);

    if (identifier.semantic->need_long_name)
    {
        String prefix = db_and_table.getQualifiedNamePrefix();
        identifier.name.insert(identifier.name.begin(), prefix.begin(), prefix.end());
    }
}

}
