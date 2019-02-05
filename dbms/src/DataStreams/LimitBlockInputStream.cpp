#include <algorithm>

#include <DataStreams/LimitBlockInputStream.h>


namespace DB
{

LimitBlockInputStream::LimitBlockInputStream(const BlockInputStreamPtr & input, size_t limit_, size_t offset_, bool always_read_till_end_)
    : limit(limit_), offset(offset_), always_read_till_end(always_read_till_end_)
{
    children.push_back(input);
}


Block LimitBlockInputStream::readImpl()
{
    Block res;
    size_t rows = 0;

    /// pos - how many rows were read, including the last read block

    if (pos >= offset + limit)
    {
        if (!always_read_till_end)
            return res;
        else
        {
            while (children.back()->read())
                ;
            return res;
        }
    }

    do
    {
        res = children.back()->read();
        if (!res)
            return res;
        rows = res.rows();
        pos += rows;
    } while (pos <= offset);

    /// return the whole block
    if (pos >= offset + rows && pos <= offset + limit)
        return res;

    /// return a piece of the block
    size_t start = std::max(
        static_cast<Int64>(0),
        static_cast<Int64>(offset) - static_cast<Int64>(pos) + static_cast<Int64>(rows));

    size_t length = std::min(
        static_cast<Int64>(limit), std::min(
        static_cast<Int64>(pos) - static_cast<Int64>(offset),
        static_cast<Int64>(limit) + static_cast<Int64>(offset) - static_cast<Int64>(pos) + static_cast<Int64>(rows)));

    for (size_t i = 0; i < res.columns(); ++i)
        res.getByPosition(i).column = res.getByPosition(i).column->cut(start, length);

    // TODO: we should provide feedback to child-block, so it will know how many rows are actually consumed.
    //       It's crucial for streaming engines like Kafka.

    return res;
}

}
