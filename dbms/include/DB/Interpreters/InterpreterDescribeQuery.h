#pragma once

#include <DB/Storages/IStorage.h>
#include <DB/Parsers/TablePropertiesQueriesASTs.h>
#include <DB/Parsers/ASTIdentifier.h>
#include <DB/Interpreters/Context.h>
#include <DB/DataStreams/OneBlockInputStream.h>
#include <DB/DataStreams/BlockIO.h>
#include <DB/DataStreams/copyData.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/DataTypes/DataTypeString.h>
#include <DB/Columns/ColumnString.h>
#include <DB/Parsers/formatAST.h>



namespace DB
{


/** Вернуть названия и типы столбцов указанной таблицы.
	*/
class InterpreterDescribeQuery
{
public:
	InterpreterDescribeQuery(ASTPtr query_ptr_, Context & context_)
		: query_ptr(query_ptr_), context(context_) {}

	BlockIO execute()
	{
		BlockIO res;
		res.in = executeImpl();
		res.in_sample = getSampleBlock();

		return res;
	}

	BlockInputStreamPtr executeAndFormat(WriteBuffer & buf)
	{
		Block sample = getSampleBlock();
		ASTPtr format_ast = typeid_cast<ASTDescribeQuery &>(*query_ptr).format;
		String format_name = format_ast ? typeid_cast<ASTIdentifier &>(*format_ast).name : context.getDefaultFormat();

		BlockInputStreamPtr in = executeImpl();
		BlockOutputStreamPtr out = context.getFormatFactory().getOutput(format_name, buf, sample);

		copyData(*in, *out);

		return in;
	}

private:
	ASTPtr query_ptr;
	Context context;

	Block getSampleBlock()
	{
		Block block;

		ColumnWithNameAndType col;
		col.name = "name";
		col.type = new DataTypeString;
		col.column = col.type->createColumn();
		block.insert(col);

		col.name = "type";
		block.insert(col);

		col.name = "default_type";
		block.insert(col);

		col.name = "default_expression";
		block.insert(col);


		return block;
	}

	BlockInputStreamPtr executeImpl()
	{
		const ASTDescribeQuery & ast = typeid_cast<const ASTDescribeQuery &>(*query_ptr);

		NamesAndTypesList columns;
		ColumnDefaults column_defaults;

		{
			StoragePtr table = context.getTable(ast.database, ast.table);
			auto table_lock = table->lockStructure(false);
			columns = table->getColumnsList();
			columns.insert(std::end(columns), std::begin(table->alias_columns), std::end(table->alias_columns));
			column_defaults = table->column_defaults;
		}

		ColumnWithNameAndType name_column{new ColumnString, new DataTypeString, "name"};
		ColumnWithNameAndType type_column{new ColumnString, new DataTypeString, "type" };
		ColumnWithNameAndType default_type_column{new ColumnString, new DataTypeString, "default_type" };
		ColumnWithNameAndType default_expression_column{new ColumnString, new DataTypeString, "default_expression" };;

		for (const auto column : columns)
		{
			name_column.column->insert(column.name);
			type_column.column->insert(column.type->getName());

			const auto it = column_defaults.find(column.name);
			if (it == std::end(column_defaults))
			{
				default_type_column.column->insertDefault();
				default_expression_column.column->insertDefault();
			}
			else
			{
				default_type_column.column->insert(toString(it->second.type));
				default_expression_column.column->insert(queryToString(it->second.expression));
			}
		}

		return new OneBlockInputStream{
			{name_column, type_column, default_type_column, default_expression_column}
		};
	}
};


}
