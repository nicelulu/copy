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
class JSONEachRowRowOutputFormat : public IRowOutputFormat
{
public:
    JSONEachRowRowOutputFormat(WriteBuffer & out_, const Block & header, const FormatSettings & settings);

    String getName() const override { return "JSONEachRowRowOutputFormat"; }

    void writeField(const IColumn & column, const IDataType & type, size_t row_num) override;
    void writeFieldDelimiter() override;
    void writeRowStartDelimiter() override;
    void writeRowEndDelimiter() override;

private:
    size_t field_number = 0;
    Names fields;

    FormatSettings settings;
};

}

