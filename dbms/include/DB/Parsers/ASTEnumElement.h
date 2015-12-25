#pragma once

#include <DB/Parsers/IAST.h>
#include <DB/Parsers/ASTLiteral.h>


namespace DB
{


class ASTEnumElement : public IAST
{
public:
	String name;
	UInt64 value;

	ASTEnumElement(const StringRange range, const String & name, const UInt64 & value)
		: IAST{range}, name{name}, value {value} {}

	String getID() const override { return "EnumElement"; }

	ASTPtr clone() const override
	{
		return new ASTEnumElement{{}, name, value};
	}

protected:
	void formatImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
	{
		frame.need_parens = false;

		const std::string indent_str = settings.one_line ? "" : std::string(4 * frame.indent, ' ');
		settings.ostr << settings.nl_or_ws << indent_str << '\'' << name << "' = " << value;
	}
};


}
