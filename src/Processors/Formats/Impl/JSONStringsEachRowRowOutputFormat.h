#pragma once

#include <Core/Block.h>
#include <IO/WriteBuffer.h>
#include <Processors/Formats/IRowOutputFormat.h>
#include <Formats/FormatSettings.h>


namespace DB
{

/** The stream for outputting data in JSON format, by object per line.
  * Does not validate UTF-8.
  */
class JSONStringsEachRowRowOutputFormat : public IRowOutputFormat
{
public:
    JSONStringsEachRowRowOutputFormat(WriteBuffer & out_, const Block & header_, FormatFactory::WriteCallback callback, const FormatSettings & settings_, bool with_names);

    String getName() const override { return "JSONStringsEachRowRowOutputFormat"; }

    void writePrefix() override;

    void writeBeforeTotals() override {}
    void writeTotals(const Columns & columns, size_t row_num) override;
    void writeAfterTotals() override {}

    void writeField(const IColumn & column, const IDataType & type, size_t row_num) override;
    void writeFieldDelimiter() override;
    void writeRowStartDelimiter() override;
    void writeRowEndDelimiter() override;

protected:
    void consumeTotals(Chunk) override;
    /// No extremes.
    void consumeExtremes(Chunk) override {}

private:
    FormatSettings settings;

    NamesAndTypes fields;

    bool with_names;
};
}
