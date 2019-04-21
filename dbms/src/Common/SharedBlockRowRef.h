#pragma once

#include <algorithm>
#include "../Core/Block.h"
#include "../../../contrib/boost/boost/smart_ptr/intrusive_ptr.hpp"
#include "../Columns/IColumn.h"


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
    struct SharedBlock : Block
    {
        int refcount = 0;

        ColumnRawPtrs all_columns;
        ColumnRawPtrs sort_columns;

        SharedBlock(Block && block) : Block(std::move(block)) {}
    };
}

inline void intrusive_ptr_add_ref(detail::SharedBlock * ptr)
{
    ++ptr->refcount;
}

inline void intrusive_ptr_release(detail::SharedBlock * ptr)
{
    if (0 == --ptr->refcount)
        delete ptr;
}

using SharedBlockPtr = boost::intrusive_ptr<detail::SharedBlock>;

struct SharedBlockRowRef
{
    ColumnRawPtrs * columns = nullptr;
    size_t row_num;
    SharedBlockPtr shared_block;

    void swap(SharedBlockRowRef & other)
    {
        std::swap(columns, other.columns);
        std::swap(row_num, other.row_num);
        std::swap(shared_block, other.shared_block);
    }

    /// The number and types of columns must match.
    bool operator==(const SharedBlockRowRef & other) const
    {
        size_t size = columns->size();
        for (size_t i = 0; i < size; ++i)
            if (0 != (*columns)[i]->compareAt(row_num, other.row_num, *(*other.columns)[i], 1))
                return false;
        return true;
    }

    bool operator!=(const SharedBlockRowRef & other) const
    {
        return !(*this == other);
    }

    void reset()
    {
        SharedBlockRowRef empty;
        swap(empty);
    }

    bool empty() const { return columns == nullptr; }
    size_t size() const { return empty() ? 0 : columns->size(); }

    /// gets pointers to all columns of block, which were used for ORDER BY
    static ColumnRawPtrs getBlockColumns(const Block & block, const SortDescription description)
    {
        size_t size = description.size();
        ColumnRawPtrs res;
        res.reserve(size);

        for (size_t i = 0; i < size; ++i)
        {
            const IColumn * column = !description[i].column_name.empty()
                                     ? block.getByName(description[i].column_name).column.get()
                                     : block.safeGetByPosition(description[i].column_number).column.get();
            res.emplace_back(column);
        }

        return res;
    }


    static void setSharedBlockRowRef(SharedBlockRowRef &row_ref, SharedBlockPtr shared_block, ColumnRawPtrs *columns,
                              size_t row_num)
    {
        row_ref.row_num = row_num;
        row_ref.columns = columns;
        row_ref.shared_block = shared_block;
    }
};



}