#include <DB/DataStreams/ExpressionBlockInputStream.h>
#include <DB/DataStreams/ProjectionBlockInputStream.h>
#include <DB/DataStreams/FilterBlockInputStream.h>
#include <DB/DataStreams/LimitBlockInputStream.h>
#include <DB/DataStreams/PartialSortingBlockInputStream.h>
#include <DB/DataStreams/MergeSortingBlockInputStream.h>
#include <DB/DataStreams/AggregatingBlockInputStream.h>
#include <DB/DataStreams/FinalizingAggregatedBlockInputStream.h>
#include <DB/DataStreams/FormatFactory.h>
#include <DB/DataStreams/copyData.h>

#include <DB/Parsers/ASTSelectQuery.h>
#include <DB/Parsers/ASTIdentifier.h>
#include <DB/Parsers/ASTFunction.h>
#include <DB/Parsers/ASTLiteral.h>
#include <DB/Parsers/ASTOrderByElement.h>

#include <DB/Interpreters/Expression.h>
#include <DB/Interpreters/InterpreterSelectQuery.h>


namespace DB
{


InterpreterSelectQuery::InterpreterSelectQuery(ASTPtr query_ptr_, Context & context_, size_t max_block_size_)
	: query_ptr(query_ptr_), context(context_), max_block_size(max_block_size_)
{
}


StoragePtr InterpreterSelectQuery::getTable()
{
	ASTSelectQuery & query = dynamic_cast<ASTSelectQuery &>(*query_ptr);
	
	/// Из какой таблицы читать данные. JOIN-ы не поддерживаются.

	String database_name;
	String table_name;

	/** Если таблица не указана - используем таблицу system.one.
	  * Если база данных не указана - используем текущую базу данных.
	  */
	if (!query.table)
	{
		database_name = "system";
		table_name = "one";
	}
	else if (!query.database)
		database_name = context.current_database;

	if (query.database)
		database_name = dynamic_cast<ASTIdentifier &>(*query.database).name;
	if (query.table)
		table_name = dynamic_cast<ASTIdentifier &>(*query.table).name;

	if (context.databases->end() == context.databases->find(database_name)
		|| (*context.databases)[database_name].end() == (*context.databases)[database_name].find(table_name))
		throw Exception("Unknown table '" + table_name + "' in database '" + database_name + "'", ErrorCodes::UNKNOWN_TABLE);

	return (*context.databases)[database_name][table_name];
}


DataTypes InterpreterSelectQuery::getReturnTypes()
{
	context.columns = getTable()->getColumns();
	Expression expression(dynamic_cast<ASTSelectQuery &>(*query_ptr).select_expression_list, context);
	return expression.getReturnTypes();
}


Block InterpreterSelectQuery::getSampleBlock()
{
	context.columns = getTable()->getColumns();
	Expression expression(dynamic_cast<ASTSelectQuery &>(*query_ptr).select_expression_list, context);
	return expression.getSampleBlock();
}


BlockInputStreamPtr InterpreterSelectQuery::execute()
{
	ASTSelectQuery & query = dynamic_cast<ASTSelectQuery &>(*query_ptr);

	StoragePtr table = getTable();
	
	/// Какие столбцы читать из этой таблицы

	context.columns = table->getColumns();
	Poco::SharedPtr<Expression> expression = new Expression(query_ptr, context);
	Names required_columns = expression->getRequiredColumns();

	/// Если не указан ни один столбец из таблицы, то будем читать первый попавшийся (чтобы хотя бы знать число строк).
	if (required_columns.empty())
		required_columns.push_back(table->getColumns().begin()->first);

	size_t limit_length = 0;
	size_t limit_offset = 0;
	if (query.limit_length)
	{
		limit_length = boost::get<UInt64>(dynamic_cast<ASTLiteral &>(*query.limit_length).value);
		if (query.limit_offset)
			limit_offset = boost::get<UInt64>(dynamic_cast<ASTLiteral &>(*query.limit_offset).value);
	}

	bool need_aggregate = expression->hasAggregates() || query.group_expression_list;

	/** Оптимизация - если не указаны WHERE, GROUP, HAVING, ORDER, но указан LIMIT, и limit + offset < max_block_size,
	  *  то в качестве размера блока будем использовать limit + offset (чтобы не читать из таблицы больше, чем запрошено).
	  */
	size_t block_size = max_block_size;
	if (!query.where_expression && !query.group_expression_list && !query.having_expression && !query.order_expression_list
		&& query.limit_length && !need_aggregate && limit_length + limit_offset < block_size)
	{
		block_size = limit_length + limit_offset;
	}

	BlockInputStreamPtr stream = table->read(required_columns, query_ptr, block_size);

	bool is_first_expression = true;

	/// Если есть условие WHERE - сначала выполним часть выражения, необходимую для его вычисления
	if (query.where_expression)
	{
		setPartID(query.where_expression, PART_WHERE);
		stream = new ExpressionBlockInputStream(stream, expression, is_first_expression, PART_WHERE);
		is_first_expression = false;
	}

	if (query.where_expression)
	{
		stream = new FilterBlockInputStream(stream);
	}

	/// Если есть GROUP BY - сначала выполним часть выражения, необходимую для его вычисления
	if (need_aggregate)
	{
		expression->markBeforeAndAfterAggregation(PART_BEFORE_AGGREGATING, PART_AFTER_AGGREGATING);

		if (query.group_expression_list)
			setPartID(query.group_expression_list, PART_GROUP);
		
		stream = new ExpressionBlockInputStream(stream, expression, is_first_expression, PART_GROUP | PART_BEFORE_AGGREGATING);
		stream = new AggregatingBlockInputStream(stream, expression);
		stream = new FinalizingAggregatedBlockInputStream(stream);
		
		is_first_expression = false;
	}

	/// Выполним оставшуюся часть выражения
	setPartID(query.select_expression_list, PART_SELECT);
	if (query.order_expression_list)
		setPartID(query.order_expression_list, PART_ORDER);
	stream = new ExpressionBlockInputStream(stream, expression, is_first_expression, PART_SELECT | PART_ORDER);
	is_first_expression = false;

	/// Оставим только столбцы, нужные для SELECT и ORDER BY части
	stream = new ProjectionBlockInputStream(stream, expression, true, PART_SELECT | PART_ORDER);
	
	/// Если есть ORDER BY
	if (query.order_expression_list)
	{
		SortDescription order_descr;
		order_descr.reserve(query.order_expression_list->children.size());
		for (ASTs::iterator it = query.order_expression_list->children.begin();
			it != query.order_expression_list->children.end();
			++it)
		{
			String name = (*it)->children.front()->getColumnName();
			order_descr.push_back(SortColumnDescription(name, dynamic_cast<ASTOrderByElement &>(**it).direction));
		}

		stream = new PartialSortingBlockInputStream(stream, order_descr);
		stream = new MergeSortingBlockInputStream(stream, order_descr);

		/// Оставим только столбцы, нужные для SELECT части
		stream = new ProjectionBlockInputStream(stream, expression, false, PART_SELECT, query.select_expression_list);
	}
	
	/// Если есть LIMIT
	if (query.limit_length)
	{
		stream = new LimitBlockInputStream(stream, limit_length, limit_offset);
	}

	return stream;
}


BlockInputStreamPtr InterpreterSelectQuery::executeAndFormat(WriteBuffer & buf)
{
	FormatFactory format_factory;
	ASTSelectQuery & query = dynamic_cast<ASTSelectQuery &>(*query_ptr);
	Block sample = getSampleBlock();
	String format_name = query.format ? dynamic_cast<ASTIdentifier &>(*query.format).name : "TabSeparated";

	BlockInputStreamPtr in = execute();
	BlockOutputStreamPtr out = format_factory.getOutput(format_name, buf, sample);
	
	copyData(*in, *out);

	return in;
}


void InterpreterSelectQuery::setPartID(ASTPtr ast, unsigned part_id)
{
	ast->part_id |= part_id;

	for (ASTs::iterator it = ast->children.begin(); it != ast->children.end(); ++it)
		setPartID(*it, part_id);
}

}
