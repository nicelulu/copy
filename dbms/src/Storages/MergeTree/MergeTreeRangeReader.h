#pragma once
#include <Core/Block.h>
#include <common/logger_useful.h>

namespace DB
{

class MergeTreeReader;

/// MergeTreeReader iterator which allows sequential reading for arbitrary number of rows between pairs of marks in the same part.
/// Stores reading state, which can be inside granule. Can skip rows in current granule and start reading from next mark.
/// Used generally for reading number of rows less than index granularity to decrease cache misses for fat blocks.
class MergeTreeRangeReader
{
public:
    MergeTreeRangeReader(MergeTreeReader * merge_tree_reader, size_t index_granularity,
                         MergeTreeRangeReader * prev_reader, ExpressionActionsPtr prewhere_actions,
                         const String * prewhere_column_name, const Names * ordered_names, bool always_reorder);

    MergeTreeRangeReader() = default;

    bool isReadingFinished() const;

    size_t numReadRowsInCurrentGranule() const;
    size_t numPendingRowsInCurrentGranule() const;

    bool isCurrentRangeFinished() const;
    bool isInitialized() const { return is_initialized; }

    class DelayedStream
    {
    public:
        DelayedStream() = default;
        DelayedStream(size_t from_mark, size_t index_granularity, MergeTreeReader * merge_tree_reader);

        /// Returns the number of rows added to block.
        /// NOTE: have to return number of rows because block has broken invariant:
        ///       some columns may have different size (for example, default columns may be zero size).
        size_t read(Block & block, size_t from_mark, size_t offset, size_t num_rows);
        size_t finalize(Block & block);

        bool isFinished() const { return is_finished; }

    private:
        size_t current_mark = 0;
        size_t current_offset = 0;
        size_t num_delayed_rows = 0;

        size_t index_granularity = 0;
        MergeTreeReader * merge_tree_reader = nullptr;
        bool continue_reading = false;
        bool is_finished = true;

        size_t position() const;
        size_t readRows(Block & block, size_t num_rows);
    };

    class Stream
    {

    public:
        Stream() = default;
        Stream(size_t from_mark, size_t to_mark, size_t index_granularity, MergeTreeReader * merge_tree_reader);

        /// Returns the n
        size_t read(Block & block, size_t num_rows, bool skip_remaining_rows_in_current_granule);
        size_t finalize(Block & block);
        void skip(size_t num_rows);

        void finish() { current_mark = last_mark; }
        bool isFinished() const { return current_mark >= last_mark; }

        size_t numReadRowsInCurrentGranule() const { return offset_after_current_mark; }
        size_t numPendingRowsInCurrentGranule() const { return index_granularity - numReadRowsInCurrentGranule(); }
        size_t numRendingGranules() const { return last_mark - current_mark; }
        size_t numPendingRows() const { return numRendingGranules() * index_granularity - offset_after_current_mark; }

    private:
        size_t current_mark = 0;
        /// Invariant: offset_after_current_mark + skipped_rows_after_offset < index_granularity
        size_t offset_after_current_mark = 0;

        size_t index_granularity = 0;
        size_t last_mark = 0;

        DelayedStream stream;

        void checkNotFinished() const;
        void checkEnoughSpaceInCurrentGranule(size_t num_rows) const;
        size_t readRows(Block & block, size_t num_rows);
    };

    class FilterWithZerosCounter
    {
    public:
        /// By default, filter is null and has always_true status.
        FilterWithZerosCounter() = default;
        explicit FilterWithZerosCounter(const ColumnPtr & filter);

        /// Can be used only if isConstant().
        const IColumn::Filter & getFilter() const;
        size_t numZeros() const { return num_zeros; }

        bool alwaysTrue() const { return always_true; }
        bool alwaysFalse() const { return always_false; }
        bool isConstant() const { return always_false || always_true; }

        void setFilter(const ColumnPtr & filter, size_t num_zeros_);

    private:
        ColumnPtr holder;
        const IColumn::Filter * filter;
        size_t num_zeros = 0;

        bool always_true = true;
        bool always_false = false;
    };

    /// Statistics after next reading step.
    class ReadResult
    {
    public:
        using NumRows = std::vector<size_t>;

        struct RangeInfo
        {
            size_t num_granules_read_before_start;
            MarkRange range;
        };

        using RangesInfo = std::vector<RangeInfo>;

        const RangesInfo & startedRanges() const { return started_ranges; }
        const NumRows & rowsPerGranule() const { return rows_per_granule; }

        /// The number of rows were read at LAST iteration in chain. <= num_added_rows + num_filtered_rows.
        size_t numReadRows() const { return num_read_rows; }
        /// The number of rows were added to block as a result of reading chain.
        size_t numAddedRows() const { return num_added_rows; }
        /// The number of filtered rows at all steps in reading chain.
        size_t numFilteredRows() const { return num_filtered_rows; }
        size_t numRowsToSkipInLastGranule() const { return num_rows_to_skip_in_last_granule; }
        /// The number of bytes read from disk.
        size_t numBytesRead() const { return num_bytes_read; }
        /// Filter you need to apply to newly-read columns in order to add them to block.
        const FilterWithZerosCounter & getFilter() const { return filter; }

        void addGranule(size_t num_rows);
        void adjustLastGranule();
        void addRows(size_t rows) { num_added_rows += rows; }
        void addRange(const MarkRange & range) { started_ranges.push_back({rows_per_granule.size(), range}); }

        /// Set filter or replace old one. Filter must have more zeroes than previous.
        void setFilter(const FilterWithZerosCounter & filter_);
        /// For each granule calculate the number of filtered rows at the end. Remove them and update filter.
        void optimize();
        /// Remove all rows from granules.
        void clear();

        void addNumBytesRead(size_t count) { num_bytes_read += count; }

        Block block;

    private:
        RangesInfo started_ranges;
        /// The number of rows read from each granule.
        NumRows rows_per_granule;
        /// Sum(rows_per_granule)
        size_t num_read_rows = 0;
        /// The number of rows was added to block while reading columns. May be zero if no read columns present in part.
        size_t num_added_rows = 0;
        /// num_zeros_in_filter + the number of rows removed after optimizes.
        size_t num_filtered_rows = 0;
        /// The number of rows was removed from last granule after clear or optimize.
        size_t num_rows_to_skip_in_last_granule = 0;
        /// Without any filtration.
        size_t num_bytes_read = 0;
        /// alwaysTrue() if prev reader hasn't prewhere_actions. Otherwise filter.size() >= num_read_rows.
        FilterWithZerosCounter filter;

        void collapseZeroTails(const IColumn::Filter & filter, IColumn::Filter & new_filter, const NumRows & zero_tails);
        size_t countZeroTails(const IColumn::Filter & filter, NumRows & zero_tails) const;
        static size_t numZerosInTail(const UInt8 * begin, const UInt8 * end);
    };

    ReadResult read(size_t max_rows, MarkRanges & ranges);

private:

    ReadResult startReadingChain(size_t max_rows, MarkRanges & ranges);
    Block continueReadingChain(ReadResult & result);
    void executePrewhereActionsAndFilterColumns(ReadResult & result);
    void filterBlock(Block & block, const FilterWithZerosCounter & filter) const;

    size_t index_granularity = 0;
    MergeTreeReader * merge_tree_reader = nullptr;

    MergeTreeRangeReader * prev_reader = nullptr; /// If not nullptr, read from prev_reader firstly.

    ExpressionActionsPtr prewhere_actions = nullptr; /// If not nullptr, calculate filter.
    const String * prewhere_column_name = nullptr;
    const Names * ordered_names = nullptr;

    Stream stream;

    bool always_reorder = true;
    bool is_initialized = false;
};

}

