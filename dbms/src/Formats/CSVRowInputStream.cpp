#include <IO/ReadHelpers.h>
#include <IO/Operators.h>

#include <Formats/verbosePrintString.h>
#include <Formats/CSVRowInputStream.h>
#include <Formats/FormatFactory.h>
#include <Formats/BlockInputStreamFromRowInputStream.h>
#include <DataTypes/DataTypeNothing.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_DATA;
}


static inline void skipEndOfLine(ReadBuffer & istr)
{
    /// \n (Unix) or \r\n (DOS/Windows) or \n\r (Mac OS Classic)

    if (*istr.position() == '\n')
    {
        ++istr.position();
        if (!istr.eof() && *istr.position() == '\r')
            ++istr.position();
    }
    else if (*istr.position() == '\r')
    {
        ++istr.position();
        if (!istr.eof() && *istr.position() == '\n')
            ++istr.position();
        else
            throw Exception("Cannot parse CSV format: found \\r (CR) not followed by \\n (LF)."
                " Line must end by \\n (LF) or \\r\\n (CR LF) or \\n\\r.", ErrorCodes::INCORRECT_DATA);
    }
    else if (!istr.eof())
        throw Exception("Expected end of line", ErrorCodes::INCORRECT_DATA);
}


static inline void skipDelimiter(ReadBuffer & istr, const char delimiter, bool is_last_column)
{
    if (is_last_column)
    {
        if (istr.eof())
            return;

        /// we support the extra delimiter at the end of the line
        if (*istr.position() == delimiter)
        {
            ++istr.position();
            if (istr.eof())
                return;
        }

        skipEndOfLine(istr);
    }
    else
        assertChar(delimiter, istr);
}


/// Skip `whitespace` symbols allowed in CSV.
static inline void skipWhitespacesAndTabs(ReadBuffer & buf)
{
    while (!buf.eof()
            && (*buf.position() == ' '
                || *buf.position() == '\t'))
        ++buf.position();
}


static void skipRow(ReadBuffer & istr, const FormatSettings::CSV & settings, size_t num_columns)
{
    String tmp;
    for (size_t i = 0; i < num_columns; ++i)
    {
        skipWhitespacesAndTabs(istr);
        readCSVString(tmp, istr, settings);
        skipWhitespacesAndTabs(istr);

        skipDelimiter(istr, settings.delimiter, i + 1 == num_columns);
    }
}


CSVRowInputStream::CSVRowInputStream(ReadBuffer & istr_, const Block & header_, bool with_names_, const FormatSettings & format_settings)
        : RowInputStreamWithDiagnosticInfo(istr_, header_), with_names(with_names_), format_settings(format_settings)
{
    const auto num_columns = header.columns();

    data_types.resize(num_columns);
    column_indexes_by_names.reserve(num_columns);

    for (size_t i = 0; i < num_columns; ++i)
    {
        const auto & column_info = header.getByPosition(i);

        data_types[i] = column_info.type;
        column_indexes_by_names.emplace(column_info.name, i);
    }
}

/// Map an input file column to a table column, based on its name.
void CSVRowInputStream::addInputColumn(const String & column_name)
{
    const auto column_it = column_indexes_by_names.find(column_name);
    if (column_it == column_indexes_by_names.end())
    {
        if (format_settings.skip_unknown_fields)
        {
            column_indexes_for_input_fields.push_back(std::nullopt);
            return;
        }

        throw Exception(
            "Unknown field found in CSV header: '" + column_name + "' " +
            "at position " + std::to_string(column_indexes_for_input_fields.size()) +
            "\nSet the 'input_format_skip_unknown_fields' parameter explicitly to ignore and proceed",
            ErrorCodes::INCORRECT_DATA
        );
    }

    const auto column_index = column_it->second;

    if (read_columns[column_index])
        throw Exception("Duplicate field found while parsing CSV header: " + column_name, ErrorCodes::INCORRECT_DATA);

    read_columns[column_index] = true;
    column_indexes_for_input_fields.emplace_back(column_index);
}

void CSVRowInputStream::readPrefix()
{
    /// In this format, we assume, that if first string field contain BOM as value, it will be written in quotes,
    ///  so BOM at beginning of stream cannot be confused with BOM in first string value, and it is safe to skip it.
    skipBOMIfExists(istr);

    if (with_names)
    {
        /// This CSV file has a header row with column names. Depending on the
        /// settings, use it or skip it.
        if (format_settings.with_names_use_header)
        {
            /// Look at the file header to see which columns we have there.
            /// The missing columns are filled with defaults.
            read_columns.assign(header.columns(), false);
            do
            {
                String column_name;
                skipWhitespacesAndTabs(istr);
                readCSVString(column_name, istr, format_settings.csv);
                skipWhitespacesAndTabs(istr);

                addInputColumn(column_name);
            }
            while (checkChar(format_settings.csv.delimiter, istr));

            skipDelimiter(istr, format_settings.csv.delimiter, true);

            for (size_t column = 0; column < read_columns.size(); column++)
            {
                if (!read_columns[column])
                {
                    have_always_default_columns = true;
                    break;
                }
            }

            return;
        }
        else
        {
            skipRow(istr, format_settings.csv, header.columns());
        }
    }

    /// The default: map each column of the file to the column of the table with
    /// the same index.
    read_columns.assign(header.columns(), true);
    column_indexes_for_input_fields.resize(header.columns());

    for (size_t i = 0; i < column_indexes_for_input_fields.size(); ++i)
    {
        column_indexes_for_input_fields[i] = i;
    }
}

/** If you change this function, don't forget to change its counterpart
  * with extended error reporting: parseRowAndPrintDiagnosticInfo().
  */
bool CSVRowInputStream::read(MutableColumns & columns, RowReadExtension & ext)
{
    if (istr.eof())
        return false;

    updateDiagnosticInfo();

    /// Track whether we have to fill any columns in this row with default
    /// values. If not, we return an empty column mask to the caller, so that
    /// it doesn't have to check it.
    bool have_default_columns = have_always_default_columns;

    const auto delimiter = format_settings.csv.delimiter;
    for (size_t file_column = 0; file_column < column_indexes_for_input_fields.size(); ++file_column)
    {
        const auto & table_column = column_indexes_for_input_fields[file_column];
        const bool is_last_file_column =
                file_column + 1 == column_indexes_for_input_fields.size();

        if (table_column)
        {
            const auto & type = data_types[*table_column];
            const bool at_delimiter = *istr.position() == delimiter;
            const bool at_last_column_line_end = is_last_file_column
                    && (*istr.position() == '\n' || *istr.position() == '\r'
                        || istr.eof());

            if (format_settings.csv.empty_as_default
                    && (at_delimiter || at_last_column_line_end))
            {
                /// Treat empty unquoted column value as default value, if
                /// specified in the settings. Tuple columns might seem
                /// problematic, because they are never quoted but still contain
                /// commas, which might be also used as delimiters. However,
                /// they do not contain empty unquoted fields, so this check
                /// works for tuples as well.
                read_columns[*table_column] = false;
                have_default_columns = true;
            }
            else
            {
                /// Read the column normally.
                read_columns[*table_column] = true;
                skipWhitespacesAndTabs(istr);
                type->deserializeAsTextCSV(*columns[*table_column], istr,
                    format_settings);
                skipWhitespacesAndTabs(istr);
            }
        }
        else
        {
            /// We never read this column from the file, just skip it.
            String tmp;
            readCSVString(tmp, istr, format_settings.csv);
        }

        skipDelimiter(istr, delimiter, is_last_file_column);
    }

    if (have_default_columns)
    {
        for (size_t i = 0; i < read_columns.size(); i++)
        {
            if (!read_columns[i])
            {
                /// The column value for this row is going to be overwritten
                /// with default by the caller, but the general assumption is
                /// that the column size increases for each row, so we have
                /// to insert something. Since we do not care about the exact
                /// value, we do not have to use the default value specified by
                /// the data type, and can just use IColumn::insertDefault().
                columns[i]->insertDefault();
            }
        }
        ext.read_columns = read_columns;
    }

    return true;
}

bool CSVRowInputStream::parseRowAndPrintDiagnosticInfo(MutableColumns & columns, WriteBuffer & out)
{
    const char delimiter = format_settings.csv.delimiter;

    for (size_t file_column = 0; file_column < column_indexes_for_input_fields.size(); ++file_column)
    {
        if (file_column == 0 && istr.eof())
        {
            out << "<End of stream>\n";
            return false;
        }

        if (column_indexes_for_input_fields[file_column].has_value())
        {
            size_t col_idx = column_indexes_for_input_fields[file_column].value();
            if (!deserializeFieldAndPrintDiagnosticInfo(header.getByPosition(col_idx).name, data_types[col_idx], *columns[col_idx],
                                                        out, file_column))
                return false;
        }
        else
        {
            static const String skipped_column_str = "<SKIPPED COLUMN>";
            static const DataTypePtr skipped_column_type = std::make_shared<DataTypeNothing>();
            static const MutableColumnPtr skipped_column = skipped_column_type->createColumn();
            if (!deserializeFieldAndPrintDiagnosticInfo(skipped_column_str, skipped_column_type, *skipped_column, out, file_column))
                return false;
        }

        /// Delimiters
        if (file_column + 1 == column_indexes_for_input_fields.size())
        {
            if (istr.eof())
                return false;

            /// we support the extra delimiter at the end of the line
            if (*istr.position() == delimiter)
            {
                ++istr.position();
                if (istr.eof())
                    break;
            }

            if (!istr.eof() && *istr.position() != '\n' && *istr.position() != '\r')
            {
                out << "ERROR: There is no line feed. ";
                verbosePrintString(istr.position(), istr.position() + 1, out);
                out << " found instead.\n"
                    " It's like your file has more columns than expected.\n"
                    "And if your file have right number of columns, maybe it have unquoted string value with comma.\n";

                return false;
            }

            skipEndOfLine(istr);
        }
        else
        {
            try
            {
                assertChar(delimiter, istr);
            }
            catch (const DB::Exception &)
            {
                if (*istr.position() == '\n' || *istr.position() == '\r')
                {
                    out << "ERROR: Line feed found where delimiter (" << delimiter << ") is expected."
                        " It's like your file has less columns than expected.\n"
                        "And if your file have right number of columns, maybe it have unescaped quotes in values.\n";
                }
                else
                {
                    out << "ERROR: There is no delimiter (" << delimiter << "). ";
                    verbosePrintString(istr.position(), istr.position() + 1, out);
                    out << " found instead.\n";
                }
                return false;
            }
        }
    }

    return true;
}


void CSVRowInputStream::syncAfterError()
{
    skipToNextLineOrEOF(istr);
}

void
CSVRowInputStream::tryDeserializeFiled(const DataTypePtr & type, IColumn & column, size_t input_position, ReadBuffer::Position & prev_pos,
                                       ReadBuffer::Position & curr_pos)
{
    skipWhitespacesAndTabs(istr);
    prev_pos = istr.position();

    if (column_indexes_for_input_fields[input_position])
    {
        const bool is_last_file_column = input_position + 1 == column_indexes_for_input_fields.size();
        const bool at_delimiter = *istr.position() == format_settings.csv.delimiter;
        const bool at_last_column_line_end = is_last_file_column
                                             && (*istr.position() == '\n' || *istr.position() == '\r' || istr.eof());

        if (format_settings.csv.empty_as_default && (at_delimiter || at_last_column_line_end))
            column.insertDefault();
        else
            type->deserializeAsTextCSV(column, istr, format_settings);
    }
    else
    {
        String tmp;
        readCSVString(tmp, istr, format_settings.csv);
    }

    curr_pos = istr.position();
    skipWhitespacesAndTabs(istr);
}


void registerInputFormatCSV(FormatFactory & factory)
{
    for (bool with_names : {false, true})
    {
        factory.registerInputFormat(with_names ? "CSVWithNames" : "CSV", [=](
            ReadBuffer & buf,
            const Block & sample,
            const Context &,
            UInt64 max_block_size,
            UInt64 rows_portion_size,
            FormatFactory::ReadCallback callback,
            const FormatSettings & settings)
        {
            return std::make_shared<BlockInputStreamFromRowInputStream>(
                std::make_shared<CSVRowInputStream>(buf, sample, with_names, settings),
                sample, max_block_size, rows_portion_size, callback, settings);
        });
    }
}

}
