#pragma once

#include <algorithm>
#include <Processors/Chunk.h>
#include <Columns/IColumn.h>
#include <boost/smart_ptr/intrusive_ptr.hpp>


namespace DB
{

/// Allows you refer to the row in the block and hold the block ownership,
///  and thus avoid creating a temporary row object.
/// Do not use std::shared_ptr, since there is no need for a place for `weak_count` and `deleter`;
///  does not use Poco::SharedPtr, since you need to allocate a block and `refcount` in one piece;
///  does not use Poco::AutoPtr, since it does not have a `move` constructor and there are extra checks for nullptr;
/// The reference counter is not atomic, since it is used from one thread.
namespace detail
{
struct SharedChunk : Chunk
{
    int refcount = 0;

    ColumnRawPtrs all_columns;
    ColumnRawPtrs sort_columns;

    SharedChunk(Chunk && chunk) : Chunk(std::move(chunk)) {}
};

}

inline void intrusive_ptr_add_ref(detail::SharedChunk * ptr)
{
    ++ptr->refcount;
}

inline void intrusive_ptr_release(detail::SharedChunk * ptr)
{
    if (0 == --ptr->refcount)
        delete ptr;
}

using SharedChunkPtr = boost::intrusive_ptr<detail::SharedChunk>;


struct SharedChunkRowRef
{
    ColumnRawPtrs * columns = nullptr;
    size_t row_num;
    SharedChunkPtr shared_block;

    void swap(SharedChunkRowRef & other)
    {
        std::swap(columns, other.columns);
        std::swap(row_num, other.row_num);
        std::swap(shared_block, other.shared_block);
    }

    /// The number and types of columns must match.
    bool operator==(const SharedChunkRowRef & other) const
    {
        size_t size = columns->size();
        for (size_t i = 0; i < size; ++i)
            if (0 != (*columns)[i]->compareAt(row_num, other.row_num, *(*other.columns)[i], 1))
                return false;
        return true;
    }

    bool operator!=(const SharedChunkRowRef & other) const
    {
        return !(*this == other);
    }

    void reset()
    {
        SharedChunkRowRef empty;
        swap(empty);
    }

    bool empty() const { return columns == nullptr; }
    size_t size() const { return empty() ? 0 : columns->size(); }

    void set(SharedChunkPtr & shared_block_, ColumnRawPtrs * columns_, size_t row_num_)
    {
        shared_block = shared_block_;
        columns = columns_;
        row_num = row_num_;
    }
};

}
