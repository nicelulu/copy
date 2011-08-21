#include <iostream>
#include <iomanip>

#include <Poco/Stopwatch.h>

#include <DB/IO/WriteBufferFromOStream.h>

#include <DB/DataTypes/DataTypesNumberFixed.h>

#include <DB/Functions/FunctionsArithmetic.h>
#include <DB/Functions/FunctionsComparison.h>

#include <DB/Parsers/ASTSelectQuery.h>
#include <DB/Parsers/ParserSelectQuery.h>
#include <DB/Parsers/formatAST.h>

#include <DB/DataStreams/TabSeparatedRowOutputStream.h>
#include <DB/DataStreams/LimitBlockInputStream.h>
#include <DB/DataStreams/copyData.h>

#include <DB/Interpreters/Expression.h>


void dump(DB::IAST & ast, int level = 0)
{
	std::string prefix(level, ' ');
	
	if (DB::ASTFunction * node = dynamic_cast<DB::ASTFunction *>(&ast))
	{
		std::cout << prefix << node << " Function, name = " << node->function->getName() << ", return types: ";
		for (DB::DataTypes::const_iterator it = node->return_types.begin(); it != node->return_types.end(); ++it)
		{
			if (it != node->return_types.begin())
				std::cout << ", ";
			std::cout << (*it)->getName();
		}
		std::cout << std::endl;
	}
	else if (DB::ASTIdentifier * node = dynamic_cast<DB::ASTIdentifier *>(&ast))
	{
		std::cout << prefix << node << " Identifier, name = " << node->name << ", type = " << node->type->getName() << std::endl;
	}
	else if (DB::ASTLiteral * node = dynamic_cast<DB::ASTLiteral *>(&ast))
	{
		std::cout << prefix << node << " Literal, " << boost::apply_visitor(DB::FieldVisitorToString(), node->value)
			<< ", type = " << node->type->getName() << std::endl;
	}

	DB::ASTs children = ast.children;
	for (DB::ASTs::iterator it = children.begin(); it != children.end(); ++it)
		dump(**it, level + 1);
}


class OneBlockInputStream : public DB::IBlockInputStream
{
private:
	const DB::Block & block;
	bool has_been_read;
public:
	OneBlockInputStream(const DB::Block & block_) : block(block_), has_been_read(false) {}

	DB::Block read()
	{
		if (!has_been_read)
		{
			has_been_read = true;
			return block;
		}
		else
			return DB::Block();
	}
};


int main(int argc, char ** argv)
{
	try
	{
		DB::ParserSelectQuery parser;
		DB::ASTPtr ast;
		std::string input = "SELECT x, s1, s2, 2 + x * 2, x * 2, x % 3 == 1, s1 == 'abc', s1 == s2";
		std::string expected;

		const char * begin = input.data();
		const char * end = begin + input.size();
		const char * pos = begin;

		if (parser.parse(pos, end, ast, expected))
		{
			std::cout << "Success." << std::endl;
			DB::formatAST(*ast, std::cout);
			std::cout << std::endl << ast->getTreeID() << std::endl;
		}
		else
		{
			std::cout << "Failed at position " << (pos - begin) << ": "
				<< mysqlxx::quote << input.substr(pos - begin, 10)
				<< ", expected " << expected << "." << std::endl;
		}

		DB::Context context;
		context.columns["x"] = new DB::DataTypeInt16;
		context.columns["s1"] = new DB::DataTypeString;
		context.columns["s2"] = new DB::DataTypeString;
		(*context.functions)["plus"] = new DB::FunctionPlus;
		(*context.functions)["multiply"] = new DB::FunctionMultiply;
		(*context.functions)["modulo"] = new DB::FunctionModulo;
		(*context.functions)["equals"] = new DB::FunctionEquals;

		DB::Expression expression(ast, context);

		dump(*ast);

		size_t n = argc == 2 ? atoi(argv[1]) : 10;

		DB::Block block;
		
		DB::ColumnWithNameAndType column_x;
		column_x.name = "x";
		column_x.type = new DB::DataTypeInt16;
		DB::ColumnInt16 * x = new DB::ColumnInt16;
		column_x.column = x;
		std::vector<Int16> & vec_x = x->getData();

		vec_x.resize(n);
		for (size_t i = 0; i < n; ++i)
			vec_x[i] = i;

		block.insert(column_x);

		DB::ColumnWithNameAndType column_s1;
		column_s1.name = "s1";
		column_s1.type = new DB::DataTypeString;
		column_s1.column = new DB::ColumnString;

		for (size_t i = 0; i < n; ++i)
			column_s1.column->insert(i % 2 ? "abc" : "def");

		block.insert(column_s1);

		DB::ColumnWithNameAndType column_s2;
		column_s2.name = "s2";
		column_s2.type = new DB::DataTypeString;
		column_s2.column = new DB::ColumnString;

		for (size_t i = 0; i < n; ++i)
			column_s2.column->insert(i % 3 ? "abc" : "def");

		block.insert(column_s2);

		{
			Poco::Stopwatch stopwatch;
			stopwatch.start();

			expression.execute(block);

			stopwatch.stop();
			std::cout << std::fixed << std::setprecision(2)
				<< "Elapsed " << stopwatch.elapsed() / 1000000.0 << " sec."
				<< ", " << n * 1000000 / stopwatch.elapsed() << " rows/sec."
				<< std::endl;
		}
		
		DB::DataTypes * data_types = new DB::DataTypes;
		for (size_t i = 0; i < block.columns(); ++i)
			data_types->push_back(block.getByPosition(i).type);

		OneBlockInputStream * is = new OneBlockInputStream(block);
		DB::LimitBlockInputStream lis(is, 10, std::max(0, static_cast<int>(n) - 10));
		DB::WriteBufferFromOStream out_buf(std::cout);
		DB::TabSeparatedRowOutputStream os(out_buf, data_types);

		DB::copyData(lis, os);
	}
	catch (const DB::Exception & e)
	{
		std::cerr << e.message() << std::endl;
	}

	return 0;
}
