#pragma once

#include <Common/Arena.h>
#include <Common/RadixSort.h>
#include <Columns/IColumn.h>

#include <optional>
#include <variant>
#include <list>
#include <mutex>
#include <algorithm>

namespace DB
{

class Block;

/// Reference to the row in block.
struct RowRef
{
    const Block * block = nullptr;
    size_t row_num = 0;

    RowRef() {}
    RowRef(const Block * block_, size_t row_num_) : block(block_), row_num(row_num_) {}
};

/// Single linked list of references to rows. Used for ALL JOINs (non-unique JOINs)
struct RowRefList : RowRef
{
    /// Portion of RowRefs, 16 * (MAX_SIZE + 1) bytes sized.
    struct Batch
    {
        static constexpr size_t MAX_SIZE = 7; /// Adequate values are 3, 7, 15, 31.

        size_t size = 0;
        Batch * next;
        RowRef row_refs[MAX_SIZE];

        Batch(Batch * parent)
            : next(parent)
        {}

        bool full() const { return size == MAX_SIZE; }

        Batch * insert(RowRef && row_ref, Arena & pool)
        {
            if (full())
            {
                auto batch = pool.alloc<Batch>();
                *batch = Batch(this);
                batch->insert(std::move(row_ref), pool);
                return batch;
            }

            row_refs[size++] = std::move(row_ref);
            return this;
        }
    };

    class ForwardIterator
    {
    public:
        ForwardIterator(const RowRefList * begin)
            : root(begin)
            , first(true)
            , batch(root->next)
            , position(0)
        {}

        const RowRef * operator -> () const
        {
            if (first)
                return root;
            return &batch->row_refs[position];
        }

        void operator ++ ()
        {
            if (first)
            {
                first = false;
                return;
            }

            if (batch)
            {
                ++position;
                if (position >= batch->size)
                {
                    batch = batch->next;
                    position = 0;
                }
            }
        }

        bool ok() const { return first || (batch && position < batch->size); }

    private:
        const RowRefList * root;
        bool first;
        Batch * batch;
        size_t position;
    };

    RowRefList() {}
    RowRefList(const Block * block_, size_t row_num_) : RowRef(block_, row_num_) {}

    ForwardIterator begin() const { return ForwardIterator(this); }

    /// insert element after current one
    void insert(RowRef && row_ref, Arena & pool)
    {
        if (!next)
        {
            next = pool.alloc<Batch>();
            *next = Batch(nullptr);
        }
        next = next->insert(std::move(row_ref), pool);
    }

private:
    Batch * next = nullptr;
};

/**
 * This class is intended to push sortable data into.
 * When looking up values the container ensures that it is sorted for log(N) lookup
 * After calling any of the lookup methods, it is no longer allowed to insert more data as this would invalidate the
 * references that can be returned by the lookup methods
 */

template <typename TEntry, typename TKey>
class SortedLookupVector
{
public:
    using Base = std::vector<TEntry>;

    // First stage, insertions into the vector
    template <typename U, typename ... TAllocatorParams>
    void insert(U && x, TAllocatorParams &&... allocator_params)
    {
        assert(!sorted.load(std::memory_order_acquire));
        array.push_back(std::forward<U>(x), std::forward<TAllocatorParams>(allocator_params)...);
    }

    // Transition into second stage, ensures that the vector is sorted
    typename Base::const_iterator upper_bound(const TEntry & k)
    {
        sort();
        return std::upper_bound(array.cbegin(), array.cend(), k);
    }

    // After ensuring that the vector is sorted by calling a lookup these are safe to call
    typename Base::const_iterator cbegin() const { return array.cbegin(); }
    typename Base::const_iterator cend() const { return array.cend(); }

private:
    std::atomic<bool> sorted = false;
    Base array;
    mutable std::mutex lock;

    struct RadixSortTraits : RadixSortNumTraits<TKey>
    {
        using Element = TEntry;
        static TKey & extractKey(Element & elem) { return elem.asof_value; }
    };

    // Double checked locking with SC atomics works in C++
    // https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/
    // The first thread that calls one of the lookup methods sorts the data
    // After calling the first lookup method it is no longer allowed to insert any data
    // the array becomes immutable
    void sort()
    {
        if (!sorted.load(std::memory_order_acquire))
        {
            std::lock_guard<std::mutex> l(lock);
            if (!sorted.load(std::memory_order_relaxed))
            {
                if (!array.empty())
                {
                    /// TODO: It has been tested only for UInt32 yet. It needs to check UInt64, Float32/64.
                    if constexpr (std::is_same_v<TKey, UInt32>)
                        RadixSort<RadixSortTraits>::executeLSD(&array[0], array.size());
                    else
                        std::sort(array.begin(), array.end());
                }

                sorted.store(true, std::memory_order_release);
            }
        }
    }
};

class AsofRowRefs
{
public:
    template <typename T>
    struct Entry
    {
        using LookupType = SortedLookupVector<Entry<T>, T>;
        using LookupPtr = std::unique_ptr<LookupType>;
        T asof_value;
        RowRef row_ref;

        Entry(T v) : asof_value(v) {}
        Entry(T v, RowRef rr) : asof_value(v), row_ref(rr) {}

        bool operator < (const Entry & o) const
        {
            return asof_value < o.asof_value;
        }
    };

    using Lookups = std::variant<
        Entry<UInt32>::LookupPtr,
        Entry<UInt64>::LookupPtr,
        Entry<Int32>::LookupPtr,
        Entry<Int64>::LookupPtr,
        Entry<Float32>::LookupPtr,
        Entry<Float64>::LookupPtr>;

    enum class Type
    {
        keyu32,
        keyu64,
        keyi32,
        keyi64,
        keyf32,
        keyf64,
    };

    AsofRowRefs() {}
    AsofRowRefs(Type t);

    static std::optional<Type> getTypeSize(const IColumn * asof_column, size_t & type_size);

    // This will be synchronized by the rwlock mutex in Join.h
    void insert(Type type, const IColumn * asof_column, const Block * block, size_t row_num);

    // This will internally synchronize
    const RowRef * findAsof(Type type, const IColumn * asof_column, size_t row_num) const;

private:
    // Lookups can be stored in a HashTable because it is memmovable
    // A std::variant contains a currently active type id (memmovable), together with a union of the types
    // The types are all std::unique_ptr, which contains a single pointer, which is memmovable.
    // Source: https://github.com/ClickHouse/ClickHouse/issues/4906
    Lookups lookups;
};

}
