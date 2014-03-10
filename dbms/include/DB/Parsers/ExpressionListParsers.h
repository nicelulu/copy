#pragma once

#include <list>

#include <DB/Parsers/IParserBase.h>
#include <DB/Parsers/CommonParsers.h>


namespace DB
{

/** Идущие подряд пары строк: оператор и соответствующая ему функция. Например, "+" -> "plus".
  * Порядок парсинга операторов имеет значение.
  */
typedef const char ** Operators_t;


/** Список элементов, разделённых чем-либо. */
class ParserList : public IParserBase
{
public:
	ParserList(ParserPtr && elem_parser_, ParserPtr && separator_parser_, bool allow_empty_ = true)
		: elem_parser(std::move(elem_parser_)), separator_parser(std::move(separator_parser_)), allow_empty(allow_empty_)
	{
	}
protected:
	const char * getName() const { return "list of elements"; }
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
private:
	ParserPtr elem_parser;
	ParserPtr separator_parser;
	bool allow_empty;
};


/** Выражение с инфиксным бинарным лево-ассоциативным оператором.
  * Например, a + b - c + d.
  */
class ParserLeftAssociativeBinaryOperatorList : public IParserBase
{
private:
	Operators_t operators;
	ParserPtr elem_parser;

public:
	/** operators_ - допустимые операторы и соответствующие им функции
	  */
	ParserLeftAssociativeBinaryOperatorList(Operators_t operators_, ParserPtr && elem_parser_)
		: operators(operators_), elem_parser(std::move(elem_parser_))
	{
	}
	
protected:
	const char * getName() const { return "list, delimited by binary operators"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


/** Выражение с инфиксным оператором произвольной арности.
  * Например, a AND b AND c AND d.
  */
class ParserVariableArityOperatorList : public IParserBase
{
private:
	ParserString infix_parser;
	const char * function_name;
	ParserPtr elem_parser;

public:
	ParserVariableArityOperatorList(const char * infix_, const char * function_, ParserPtr && elem_parser_)
		: infix_parser(infix_, true, true), function_name(function_), elem_parser(std::move(elem_parser_))
	{
	}

protected:
	const char * getName() const { return "list, delimited by operator of variable arity"; }

	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


/** Выражение с префиксным унарным оператором.
  * Например, NOT x.
  */
class ParserPrefixUnaryOperatorExpression : public IParserBase
{
private:
	Operators_t operators;
	ParserPtr elem_parser;

public:
	/** operators_ - допустимые операторы и соответствующие им функции
	  */
	ParserPrefixUnaryOperatorExpression(Operators_t operators_, ParserPtr && elem_parser_)
		: operators(operators_), elem_parser(std::move(elem_parser_))
	{
	}
	
protected:
	const char * getName() const { return "expression with prefix unary operator"; }
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserAccessExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserLeftAssociativeBinaryOperatorList operator_parser;
public:
	ParserAccessExpression();
	
protected:
	const char * getName() const { return "access expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserUnaryMinusExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserPrefixUnaryOperatorExpression operator_parser {operators, ParserPtr(new ParserAccessExpression)};
	
protected:
	const char * getName() const { return "unary minus expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserMultiplicativeExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserLeftAssociativeBinaryOperatorList operator_parser {operators, ParserPtr(new ParserUnaryMinusExpression)};

protected:
	const char * getName() const { return "multiplicative expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserAdditiveExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserLeftAssociativeBinaryOperatorList operator_parser {operators, ParserPtr(new ParserMultiplicativeExpression)};
	
protected:
	const char * getName() const { return "additive expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserComparisonExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserLeftAssociativeBinaryOperatorList operator_parser {operators, ParserPtr(new ParserAdditiveExpression)};
	
protected:
	const char * getName() const { return "comparison expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserLogicalNotExpression : public IParserBase
{
private:
	static const char * operators[];
	ParserPrefixUnaryOperatorExpression operator_parser {operators, ParserPtr(new ParserComparisonExpression)};

protected:
	const char * getName() const { return "logical-NOT expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserLogicalAndExpression : public IParserBase
{
private:
	ParserVariableArityOperatorList operator_parser;
public:
	ParserLogicalAndExpression()
		: operator_parser("AND", "and", ParserPtr(new ParserLogicalNotExpression))
	{
	}
	
protected:
	const char * getName() const { return "logical-AND expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


class ParserLogicalOrExpression : public IParserBase
{
private:
	ParserVariableArityOperatorList operator_parser;
public:
	ParserLogicalOrExpression()
		: operator_parser("OR", "or", ParserPtr(new ParserLogicalAndExpression))
	{
	}
	
protected:
	const char * getName() const { return "logical-OR expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return operator_parser.parse(pos, end, node, expected);
	}
};


/** Выражение с тернарным оператором.
  * Например, a = 1 ? b + 1 : c * 2.
  */
class ParserTernaryOperatorExpression : public IParserBase
{
private:
	ParserLogicalOrExpression elem_parser;

protected:
	const char * getName() const { return "expression with ternary operator"; }

	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserLambdaExpression : public IParserBase
{
private:
	ParserTernaryOperatorExpression elem_parser;
	
protected:
	const char * getName() const { return "lambda expression"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserExpressionWithOptionalAlias : public IParserBase
{
public:
	ParserExpressionWithOptionalAlias();
protected:
	ParserPtr impl;

	const char * getName() const { return "expression with optional alias"; }
	
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
	{
		return impl->parse(pos, end, node, expected);
	}
};


/** Список выражений, разделённых запятыми, возможно пустой. */
class ParserExpressionList : public IParserBase
{
protected:
	const char * getName() const { return "list of expressions"; }
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserNotEmptyExpressionList : public IParserBase
{
private:
	ParserExpressionList nested_parser;
protected:
	const char * getName() const { return "not empty list of expressions"; }
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


class ParserOrderByExpressionList : public IParserBase
{
protected:
	const char * getName() const { return "order by expression"; }
	bool parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected);
};


}
