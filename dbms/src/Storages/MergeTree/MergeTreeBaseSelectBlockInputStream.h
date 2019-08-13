#pragma once

#include <DataStreams/IBlockInputStream.h>
#include <Storages/MergeTree/MergeTreeBlockReadUtils.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Storages/SelectQueryInfo.h>

namespace DB
{

class MergeTreeReader;
class UncompressedCache;
class MarkCache;


/// Base class for MergeTreeThreadSelectBlockInputStream and MergeTreeSelectBlockInputStream
class MergeTreeBaseSelectBlockInputStream : public IBlockInputStream
{
public:
    MergeTreeBaseSelectBlockInputStream(
        const MergeTreeData & storage_,
        const PrewhereInfoPtr & prewhere_info_,
        UInt64 max_block_size_rows_,
        UInt64 preferred_block_size_bytes_,
        UInt64 preferred_max_column_in_block_size_bytes_,
        UInt64 min_bytes_to_use_direct_io_,
        UInt64 max_read_buffer_size_,
        bool use_uncompressed_cache_,
        bool save_marks_in_cache_ = true,
        const Names & virt_column_names_ = {});

    ~MergeTreeBaseSelectBlockInputStream() override;

    static void executePrewhereActions(Block & block, const PrewhereInfoPtr & prewhere_info);

protected:
    Block readImpl() final;

    /// Creates new this->task, and initilizes readers
    virtual bool getNewTask() = 0;

    /// We will call progressImpl manually.
    void progress(const Progress &) override {}

    virtual Block readFromPart();

    Block readFromPartImpl();

    void injectVirtualColumns(Block & block) const;

    void initializeRangeReaders(MergeTreeReadTask & task);

    size_t estimateNumRows(MergeTreeReadTask & current_task, MergeTreeRangeReader & current_reader);

protected:
    const MergeTreeData & storage;

    PrewhereInfoPtr prewhere_info;

    UInt64 max_block_size_rows;
    UInt64 preferred_block_size_bytes;
    UInt64 preferred_max_column_in_block_size_bytes;

    UInt64 min_bytes_to_use_direct_io;
    UInt64 max_read_buffer_size;

    bool use_uncompressed_cache;
    bool save_marks_in_cache;

    Names virt_column_names;

    std::unique_ptr<MergeTreeReadTask> task;

    std::shared_ptr<UncompressedCache> owned_uncompressed_cache;
    std::shared_ptr<MarkCache> owned_mark_cache;

    using MergeTreeReaderPtr = std::unique_ptr<MergeTreeReader>;
    MergeTreeReaderPtr reader;
    MergeTreeReaderPtr pre_reader;
};

}
