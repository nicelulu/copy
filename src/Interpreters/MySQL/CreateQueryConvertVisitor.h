#pragma once

#include <Core/NamesAndTypes.h>
#include <Interpreters/InDepthNodeVisitor.h>
#include <Parsers/MySQL/ASTCreateQuery.h>
#include <Parsers/MySQL/ASTCreateDefines.h>
#include <Parsers/MySQL/ASTDeclareIndex.h>
#include <Parsers/MySQL/ASTDeclareColumn.h>
#include <Parsers/MySQL/ASTDeclarePartitionOptions.h>

namespace DB
{

namespace MySQLVisitor
{

/// Convert MySQL CREATE query to https://clickhouse.tech/docs/en/sql-reference/statements/create/
class CreateQueryMatcher
{
public:
    using Visitor = InDepthNodeVisitor<CreateQueryMatcher, false>;

    struct Data
    {
        /// SETTINGS
        WriteBuffer & out;
        const Context & context;
        size_t max_ranges;
        size_t min_rows_pre_range;

        ASTs primary_keys;
        ASTs partition_keys;
        NamesAndTypesList columns_name_and_type;

        void addPrimaryKey(const ASTPtr & primary_key);

        void addPartitionKey(const ASTPtr & partition_key);

        ASTPtr getFormattedOrderByExpression();

        ASTPtr getFormattedPartitionByExpression();
    };

    static void visit(ASTPtr & ast, Data & data);

    static bool needChildVisit(ASTPtr &, const ASTPtr &) { return false; }
private:
    static void visit(const MySQLParser::ASTCreateQuery & create, const ASTPtr &, Data & data);

    static void visit(const MySQLParser::ASTDeclareIndex & declare_index, const ASTPtr &, Data & data);

    static void visit(const MySQLParser::ASTCreateDefines & create_defines, const ASTPtr &, Data & data);

    static void visit(const MySQLParser::ASTDeclareColumn & declare_column, const ASTPtr &, Data & data);

    static void visit(const MySQLParser::ASTDeclarePartitionOptions & declare_partition_options, const ASTPtr &, Data & data);
};

using CreateQueryConvertVisitor = CreateQueryMatcher::Visitor;

}



}
