#pragma once

#include <DB/Parsers/IAST.h>
#include <DB/DataTypes/IDataType.h>
#include <DB/Common/UInt128.h>
#include <unordered_map>


namespace DB
{

class Context;
class WriteBuffer;
class CollectAliases;
class AnalyzeColumns;
class IFunction;
class IAggregateFunction;


/** For every expression, deduce its type,
  *  and if it is a constant expression, calculate its value.
  *
  * Types and constants inference goes together,
  *  because sometimes resulting type of a function depend on value of constant expression.
  * Notable examples: tupleElement(tuple, N) and toFixedString(s, N) functions.
  *
  * Also creates and stores function objects.
  * Also calculate ids for expressions, that will identify common subexpressions.
  */
struct TypeAndConstantInference
{
	void process(ASTPtr & ast, Context & context, CollectAliases & aliases, const AnalyzeColumns & columns);

	struct ExpressionInfo
	{
		/// Must identify identical expressions.
		/// For example following two expressions in query are the same: SELECT sum(x) AS a, SUM(t.x) AS b FROM t
		UInt128 id {};
		ASTPtr node;
		DataTypePtr data_type;
		bool is_constant_expression = false;
		Field value;	/// Has meaning if is_constant_expression == true.
		std::shared_ptr<IFunction> function;
		std::shared_ptr<IAggregateFunction> aggregate_function;
	};

	/// Key is getColumnName of AST node.
	using Info = std::unordered_map<String, ExpressionInfo>;
	Info info;

	/// Debug output
	void dump(WriteBuffer & out) const;
};

}
