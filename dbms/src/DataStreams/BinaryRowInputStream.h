#pragma once

#include <DataStreams/IRowInputStream.h>


namespace DB
{

class Block;
class ReadBuffer;


/** A stream for inputting data in a binary line-by-line format.
  */
class BinaryRowInputStream : public IRowInputStream
{
public:
    BinaryRowInputStream(ReadBuffer & istr_, const Block & header_);

    bool read(MutableColumns & columns) override;

private:
    ReadBuffer & istr;
    Block header;
};

}
