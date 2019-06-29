#include <Parsers/ASTWithAlias.h>
#include <IO/WriteBufferFromOStream.h>
#include <IO/WriteHelpers.h>


namespace DB
{

void ASTWithAlias::writeAlias(const String & name, const FormatSettings & settings) const
{
    settings.ostr << (settings.hilite ? hilite_keyword : "") << " AS " << (settings.hilite ? hilite_alias : "");
    settings.writeIdentifier(name);
    settings.ostr << (settings.hilite ? hilite_none : "");
}


void ASTWithAlias::formatImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    /// We will compare formatting result with previously formatted nodes.
    std::stringstream temporary_buffer;
    FormatSettings temporary_settings(temporary_buffer, settings);

    /// If there is an alias, then parentheses are required around the entire expression, including the alias.
    /// Because a record of the form `0 AS x + 0` is syntactically invalid.
    if (frame.need_parens && !alias.empty())
        temporary_buffer << '(';

    formatImplWithoutAlias(temporary_settings, state, frame);

    /// If we have previously output this node elsewhere in the query, now it is enough to output only the alias.
    /// This is needed because the query can become extraordinary large after substitution of aliases.
    if (!alias.empty() && !state.printed_asts_with_alias.emplace(frame.current_select, alias, temporary_buffer.str()).second)
    {
        settings.writeIdentifier(alias);
    }
    else
    {
        settings.ostr << temporary_buffer.rdbuf();

        if (!alias.empty())
        {
            writeAlias(alias, settings);
            if (frame.need_parens)
                settings.ostr << ')';
        }
    }
}

void ASTWithAlias::appendColumnName(WriteBuffer & ostr) const
{
    if (prefer_alias_to_column_name && !alias.empty())
        writeString(alias, ostr);
    else
        appendColumnNameImpl(ostr);
}

}
