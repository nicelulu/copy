#include <Storages/MergeTree/MergeTreeDataPartWriterOnDisk.h>

#include <utility>

namespace DB
{
namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

namespace
{
    constexpr auto INDEX_FILE_EXTENSION = ".idx";
}

Granules getGranulesToWrite(const MergeTreeIndexGranularity & index_granularity, size_t block_rows, size_t current_mark, size_t rows_written_in_last_mark)
{
    if (current_mark >= index_granularity.getMarksCount())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Request to get granules from mark {} but index granularity size is {}", current_mark, index_granularity.getMarksCount());

    Granules result;
    size_t current_row = 0;
    if (rows_written_in_last_mark > 0)
    {
        size_t rows_left_in_last_mark = index_granularity.getMarkRows(current_mark) - rows_written_in_last_mark;
        size_t rest_rows = block_rows - current_row;
        if (rest_rows < rows_left_in_last_mark)
            result.emplace_back(Granule{current_row, rows_left_in_last_mark, rest_rows, current_mark, false, false});
        else
            result.emplace_back(Granule{current_row, rows_left_in_last_mark, rows_left_in_last_mark, current_mark, false, true});
        current_row += rows_left_in_last_mark;
        current_mark++;
    }

    while (current_row < block_rows)
    {
        size_t expected_rows = index_granularity.getMarkRows(current_mark);
        size_t rest_rows = block_rows - current_row;
        if (rest_rows < expected_rows)
            result.emplace_back(Granule{current_row, expected_rows, rest_rows, current_mark, true, false});
        else
            result.emplace_back(Granule{current_row, expected_rows, expected_rows, current_mark, true, true});

        current_row += expected_rows;
        current_mark++;
    }

    return result;
}

void MergeTreeDataPartWriterOnDisk::Stream::finalize()
{
    compressed.next();
    /// 'compressed_buf' doesn't call next() on underlying buffer ('plain_hashing'). We should do it manually.
    plain_hashing.next();
    marks.next();

    plain_file->finalize();
    marks_file->finalize();
}

void MergeTreeDataPartWriterOnDisk::Stream::sync() const
{
    plain_file->sync();
    marks_file->sync();
}

MergeTreeDataPartWriterOnDisk::Stream::Stream(
    const String & escaped_column_name_,
    DiskPtr disk_,
    const String & data_path_,
    const std::string & data_file_extension_,
    const std::string & marks_path_,
    const std::string & marks_file_extension_,
    const CompressionCodecPtr & compression_codec_,
    size_t max_compress_block_size_,
    size_t estimated_size_,
    size_t aio_threshold_) :
    escaped_column_name(escaped_column_name_),
    data_file_extension{data_file_extension_},
    marks_file_extension{marks_file_extension_},
    plain_file(disk_->writeFile(data_path_ + data_file_extension, max_compress_block_size_, WriteMode::Rewrite, estimated_size_, aio_threshold_)),
    plain_hashing(*plain_file), compressed_buf(plain_hashing, compression_codec_), compressed(compressed_buf),
    marks_file(disk_->writeFile(marks_path_ + marks_file_extension, 4096, WriteMode::Rewrite)), marks(*marks_file)
{
}

void MergeTreeDataPartWriterOnDisk::Stream::addToChecksums(MergeTreeData::DataPart::Checksums & checksums)
{
    String name = escaped_column_name;

    checksums.files[name + data_file_extension].is_compressed = true;
    checksums.files[name + data_file_extension].uncompressed_size = compressed.count();
    checksums.files[name + data_file_extension].uncompressed_hash = compressed.getHash();
    checksums.files[name + data_file_extension].file_size = plain_hashing.count();
    checksums.files[name + data_file_extension].file_hash = plain_hashing.getHash();

    checksums.files[name + marks_file_extension].file_size = marks.count();
    checksums.files[name + marks_file_extension].file_hash = marks.getHash();
}


MergeTreeDataPartWriterOnDisk::MergeTreeDataPartWriterOnDisk(
    const MergeTreeData::DataPartPtr & data_part_,
    const NamesAndTypesList & columns_list_,
    const StorageMetadataPtr & metadata_snapshot_,
    const MergeTreeIndices & indices_to_recalc_,
    const String & marks_file_extension_,
    const CompressionCodecPtr & default_codec_,
    const MergeTreeWriterSettings & settings_,
    const MergeTreeIndexGranularity & index_granularity_)
    : IMergeTreeDataPartWriter(data_part_,
        columns_list_, metadata_snapshot_, settings_, index_granularity_)
    , skip_indices(indices_to_recalc_)
    , part_path(data_part_->getFullRelativePath())
    , marks_file_extension(marks_file_extension_)
    , default_codec(default_codec_)
    , compute_granularity(index_granularity.empty())
{
    if (settings.blocks_are_granules_size && !index_granularity.empty())
        throw Exception("Can't take information about index granularity from blocks, when non empty index_granularity array specified", ErrorCodes::LOGICAL_ERROR);

    auto disk = data_part->volume->getDisk();
    if (!disk->exists(part_path))
        disk->createDirectories(part_path);

    if (settings.rewrite_primary_key)
        initPrimaryIndex();
    initSkipIndices();
}

// Implementation is split into static functions for ability
/// of making unit tests without creation instance of IMergeTreeDataPartWriter,
/// which requires a lot of dependencies and access to filesystem.
static size_t computeIndexGranularityImpl(
    const Block & block,
    size_t index_granularity_bytes,
    size_t fixed_index_granularity_rows,
    bool blocks_are_granules,
    bool can_use_adaptive_index_granularity)
{
    size_t rows_in_block = block.rows();
    size_t index_granularity_for_block;
    if (!can_use_adaptive_index_granularity)
        index_granularity_for_block = fixed_index_granularity_rows;
    else
    {
        size_t block_size_in_memory = block.bytes();
        if (blocks_are_granules)
            index_granularity_for_block = rows_in_block;
        else if (block_size_in_memory >= index_granularity_bytes)
        {
            size_t granules_in_block = block_size_in_memory / index_granularity_bytes;
            index_granularity_for_block = rows_in_block / granules_in_block;
        }
        else
        {
            size_t size_of_row_in_bytes = block_size_in_memory / rows_in_block;
            index_granularity_for_block = index_granularity_bytes / size_of_row_in_bytes;
        }
    }
    if (index_granularity_for_block == 0) /// very rare case when index granularity bytes less then single row
        index_granularity_for_block = 1;

    /// We should be less or equal than fixed index granularity
    index_granularity_for_block = std::min(fixed_index_granularity_rows, index_granularity_for_block);
    return index_granularity_for_block;
}

size_t MergeTreeDataPartWriterOnDisk::computeIndexGranularity(const Block & block) const
{
    const auto storage_settings = storage.getSettings();
    return computeIndexGranularityImpl(
            block,
            storage_settings->index_granularity_bytes,
            storage_settings->index_granularity,
            settings.blocks_are_granules_size,
            settings.can_use_adaptive_granularity);
}

void MergeTreeDataPartWriterOnDisk::initPrimaryIndex()
{
    if (metadata_snapshot->hasPrimaryKey())
    {
        index_file_stream = data_part->volume->getDisk()->writeFile(part_path + "primary.idx", DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite);
        index_stream = std::make_unique<HashingWriteBuffer>(*index_file_stream);
    }
}

void MergeTreeDataPartWriterOnDisk::initSkipIndices()
{
    for (const auto & index_helper : skip_indices)
    {
        String stream_name = index_helper->getFileName();
        skip_indices_streams.emplace_back(
                std::make_unique<MergeTreeDataPartWriterOnDisk::Stream>(
                        stream_name,
                        data_part->volume->getDisk(),
                        part_path + stream_name, INDEX_FILE_EXTENSION,
                        part_path + stream_name, marks_file_extension,
                        default_codec, settings.max_compress_block_size,
                        0, settings.aio_threshold));
        skip_indices_aggregators.push_back(index_helper->createIndexAggregator());
        skip_index_filling.push_back(0);
    }
}

void MergeTreeDataPartWriterOnDisk::calculateAndSerializePrimaryIndex(const Block & primary_index_block, const Granules & granules_to_write)
{
    size_t primary_columns_num = primary_index_block.columns();
    if (index_columns.empty())
    {
        index_types = primary_index_block.getDataTypes();
        index_columns.resize(primary_columns_num);
        last_block_index_columns.resize(primary_columns_num);
        for (size_t i = 0; i < primary_columns_num; ++i)
            index_columns[i] = primary_index_block.getByPosition(i).column->cloneEmpty();
    }

    /** While filling index (index_columns), disable memory tracker.
     * Because memory is allocated here (maybe in context of INSERT query),
     *  but then freed in completely different place (while merging parts), where query memory_tracker is not available.
     * And otherwise it will look like excessively growing memory consumption in context of query.
     *  (observed in long INSERT SELECTs)
     */
    MemoryTracker::BlockerInThread temporarily_disable_memory_tracker;

    /// Write index. The index contains Primary Key value for each `index_granularity` row.
    for (const auto & granule : granules_to_write)
    {
        if (metadata_snapshot->hasPrimaryKey() && granule.mark_on_start)
        {
            for (size_t j = 0; j < primary_columns_num; ++j)
            {
                const auto & primary_column = primary_index_block.getByPosition(j);
                index_columns[j]->insertFrom(*primary_column.column, granule.start);
                primary_column.type->serializeBinary(*primary_column.column, granule.start, *index_stream);
            }
        }
    }
    /// store last index row to write final mark at the end of column
    for (size_t j = 0; j < primary_columns_num; ++j)
        last_block_index_columns[j] = primary_index_block.getByPosition(j).column;
}

void MergeTreeDataPartWriterOnDisk::calculateAndSerializeSkipIndices(const Block & skip_indexes_block, const Granules & granules_to_write)
{
    /// Filling and writing skip indices like in MergeTreeDataPartWriterWide::writeColumn
    for (size_t i = 0; i < skip_indices.size(); ++i)
    {
        const auto index_helper = skip_indices[i];
        auto & stream = *skip_indices_streams[i];
        for (const auto & granule : granules_to_write)
        {
            if (skip_indices_aggregators[i]->empty() && granule.mark_on_start)
            {
                skip_indices_aggregators[i] = index_helper->createIndexAggregator();
                skip_index_filling[i] = 0;

                if (stream.compressed.offset() >= settings.min_compress_block_size)
                    stream.compressed.next();

                writeIntBinary(stream.plain_hashing.count(), stream.marks);
                writeIntBinary(stream.compressed.offset(), stream.marks);
                /// Actually this numbers is redundant, but we have to store them
                /// to be compatible with normal .mrk2 file format
                if (settings.can_use_adaptive_granularity)
                    writeIntBinary(1UL, stream.marks);
            }

            size_t pos = granule.start;
            skip_indices_aggregators[i]->update(skip_indexes_block, &pos, granule.rows_count);
            if (granule.is_completed)
            {
                ++skip_index_filling[i];

                /// write index if it is filled
                if (skip_index_filling[i] == index_helper->index.granularity)
                {
                    skip_indices_aggregators[i]->getGranuleAndReset()->serializeBinary(stream.compressed);
                    skip_index_filling[i] = 0;
                }
            }
        }
    }
}

void MergeTreeDataPartWriterOnDisk::finishPrimaryIndexSerialization(
        MergeTreeData::DataPart::Checksums & checksums, bool sync)
{
    bool write_final_mark = (with_final_mark && data_written);
    if (write_final_mark && compute_granularity)
        index_granularity.appendMark(0);

    if (index_stream)
    {
        if (write_final_mark)
        {
            for (size_t j = 0; j < index_columns.size(); ++j)
            {
                const auto & column = *last_block_index_columns[j];
                size_t last_row_number = column.size() - 1;
                index_columns[j]->insertFrom(column, last_row_number);
                index_types[j]->serializeBinary(column, last_row_number, *index_stream);
            }
            last_block_index_columns.clear();
        }

        index_stream->next();
        checksums.files["primary.idx"].file_size = index_stream->count();
        checksums.files["primary.idx"].file_hash = index_stream->getHash();
        index_file_stream->finalize();
        if (sync)
            index_file_stream->sync();
        index_stream = nullptr;
    }
}

void MergeTreeDataPartWriterOnDisk::finishSkipIndicesSerialization(
        MergeTreeData::DataPart::Checksums & checksums, bool sync)
{
    for (size_t i = 0; i < skip_indices.size(); ++i)
    {
        auto & stream = *skip_indices_streams[i];
        if (!skip_indices_aggregators[i]->empty())
            skip_indices_aggregators[i]->getGranuleAndReset()->serializeBinary(stream.compressed);
    }

    for (auto & stream : skip_indices_streams)
    {
        stream->finalize();
        stream->addToChecksums(checksums);
        if (sync)
            stream->sync();
    }

    skip_indices_streams.clear();
    skip_indices_aggregators.clear();
    skip_index_filling.clear();
}

Names MergeTreeDataPartWriterOnDisk::getSkipIndicesColumns() const
{
    std::unordered_set<String> skip_indexes_column_names_set;
    for (const auto & index : skip_indices)
        std::copy(index->index.column_names.cbegin(), index->index.column_names.cend(),
                  std::inserter(skip_indexes_column_names_set, skip_indexes_column_names_set.end()));
    return Names(skip_indexes_column_names_set.begin(), skip_indexes_column_names_set.end());
}

}
