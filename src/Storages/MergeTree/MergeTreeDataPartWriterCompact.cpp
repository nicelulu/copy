#include <Storages/MergeTree/MergeTreeDataPartWriterCompact.h>
#include <Storages/MergeTree/MergeTreeDataPartCompact.h>

namespace DB
{
MergeTreeDataPartWriterCompact::MergeTreeDataPartWriterCompact(
    const MergeTreeData::DataPartPtr & data_part_,
    const NamesAndTypesList & columns_list_,
    const StorageMetadataPtr & metadata_snapshot_,
    const std::vector<MergeTreeIndexPtr> & indices_to_recalc_,
    const String & marks_file_extension_,
    const CompressionCodecPtr & default_codec_,
    const MergeTreeWriterSettings & settings_,
    const MergeTreeIndexGranularity & index_granularity_)
    : MergeTreeDataPartWriterOnDisk(data_part_, columns_list_, metadata_snapshot_,
        indices_to_recalc_, marks_file_extension_,
        default_codec_, settings_, index_granularity_)
    , plain_file(data_part->volume->getDisk()->writeFile(
            part_path + MergeTreeDataPartCompact::DATA_FILE_NAME_WITH_EXTENSION,
            settings.max_compress_block_size, 
            WriteMode::Rewrite,
            settings.estimated_size,
            settings.aio_threshold))
    , plain_hashing(*plain_file)
    , marks_file(data_part->volume->getDisk()->writeFile(
        part_path + MergeTreeDataPartCompact::DATA_FILE_NAME + marks_file_extension_,
        4096,
        WriteMode::Rewrite))
    , marks(*marks_file)
{
    const auto & storage_columns = metadata_snapshot->getColumns();
    for (const auto & column : columns_list)
        compressed_streams[column.name] = std::make_unique<CompressedStream>(
            plain_hashing, storage_columns.getCodecOrDefault(column.name, default_codec)); 
}

void MergeTreeDataPartWriterCompact::write(
    const Block & block, const IColumn::Permutation * permutation,
    const Block & primary_key_block, const Block & skip_indexes_block)
{
    /// Fill index granularity for this block
    /// if it's unknown (in case of insert data or horizontal merge,
    /// but not in case of vertical merge)
    if (compute_granularity)
    {
        size_t index_granularity_for_block = computeIndexGranularity(block);
        fillIndexGranularity(index_granularity_for_block, block.rows());
    }

    Block result_block;

    if (permutation)
    {
        for (const auto & it : columns_list)
        {
            if (primary_key_block.has(it.name))
                result_block.insert(primary_key_block.getByName(it.name));
            else if (skip_indexes_block.has(it.name))
                result_block.insert(skip_indexes_block.getByName(it.name));
            else
            {
                auto column = block.getByName(it.name);
                column.column = column.column->permute(*permutation, 0);
                result_block.insert(column);
            }
        }
    }
    else
    {
        result_block = block;
    }

    if (!header)
        header = result_block.cloneEmpty();

    columns_buffer.add(result_block.mutateColumns());
    size_t last_mark_rows = index_granularity.getLastMarkRows();
    size_t rows_in_buffer = columns_buffer.size();

    if (rows_in_buffer < last_mark_rows)
    {
        /// If it's not enough rows for granule, accumulate blocks
        ///  and save how much rows we already have.
        next_index_offset = last_mark_rows - rows_in_buffer;
        return;
    }

    writeBlock(header.cloneWithColumns(columns_buffer.releaseColumns()));
}

void MergeTreeDataPartWriterCompact::writeBlock(const Block & block)
{
    size_t total_rows = block.rows();
    size_t from_mark = getCurrentMark();
    size_t current_row = 0;

    while (current_row < total_rows)
    {
        size_t rows_to_write = index_granularity.getMarkRows(from_mark);

        if (rows_to_write)
            data_written = true;

        for (const auto & column : columns_list)
        {
            auto & stream = compressed_streams[column.name];

            writeIntBinary(plain_hashing.count(), marks);
            writeIntBinary(UInt64(0), marks);

            writeColumnSingleGranule(block.getByName(column.name), current_row, rows_to_write);
            stream->hashing_buf.next();
        }

        ++from_mark;
        size_t rows_written = total_rows - current_row;
        current_row += rows_to_write;

        /// Correct last mark as it should contain exact amount of rows.
        if (current_row >= total_rows && rows_written != rows_to_write)
        {
            rows_to_write = rows_written;
            index_granularity.popMark();
            index_granularity.appendMark(rows_written);
        }

        writeIntBinary(rows_to_write, marks);
    }

    next_index_offset = 0;
    next_mark = from_mark;
}

void MergeTreeDataPartWriterCompact::writeColumnSingleGranule(const ColumnWithTypeAndName & column, size_t from_row, size_t number_of_rows) const
{
    IDataType::SerializeBinaryBulkStatePtr state;
    IDataType::SerializeBinaryBulkSettings serialize_settings;

    serialize_settings.getter = [this, &column](IDataType::SubstreamPath) -> WriteBuffer * { return &compressed_streams.at(column.name)->hashing_buf; };
    serialize_settings.position_independent_encoding = true;
    serialize_settings.low_cardinality_max_dictionary_size = 0;

    column.type->serializeBinaryBulkStatePrefix(serialize_settings, state);
    column.type->serializeBinaryBulkWithMultipleStreams(*column.column, from_row, number_of_rows, serialize_settings, state);
    column.type->serializeBinaryBulkStateSuffix(serialize_settings, state);
}

void MergeTreeDataPartWriterCompact::finishDataSerialization(IMergeTreeDataPart::Checksums & checksums)
{
    if (columns_buffer.size() != 0)
        writeBlock(header.cloneWithColumns(columns_buffer.releaseColumns()));

    if (with_final_mark && data_written)
    {
        for (size_t i = 0; i < columns_list.size(); ++i)
        {
            writeIntBinary(plain_hashing.count(), marks);
            writeIntBinary(UInt64(0), marks);
        }
        writeIntBinary(UInt64(0), marks);
    }

    plain_file->next();
    marks.next();
    addToChecksums(checksums);
}

static void fillIndexGranularityImpl(
    MergeTreeIndexGranularity & index_granularity,
    size_t index_offset,
    size_t index_granularity_for_block,
    size_t rows_in_block)
{
    for (size_t current_row = index_offset; current_row < rows_in_block; current_row += index_granularity_for_block)
    {
        size_t rows_left_in_block = rows_in_block - current_row;

        /// Try to extend last granule if block is large enough
        ///  or it isn't first in granule (index_offset != 0).
        if (rows_left_in_block < index_granularity_for_block &&
            (rows_in_block >= index_granularity_for_block || index_offset != 0))
        {
            // If enough rows are left, create a new granule. Otherwise, extend previous granule.
            // So, real size of granule differs from index_granularity_for_block not more than 50%.
            if (rows_left_in_block * 2 >= index_granularity_for_block)
                index_granularity.appendMark(rows_left_in_block);
            else
                index_granularity.addRowsToLastMark(rows_left_in_block);
        }
        else
        {
            index_granularity.appendMark(index_granularity_for_block);
        }
    }
}

void MergeTreeDataPartWriterCompact::fillIndexGranularity(size_t index_granularity_for_block, size_t rows_in_block)
{
    fillIndexGranularityImpl(
        index_granularity,
        getIndexOffset(),
        index_granularity_for_block,
        rows_in_block);
}

void MergeTreeDataPartWriterCompact::addToChecksums(MergeTreeDataPartChecksums & checksums)
{
    using uint128 = CityHash_v1_0_2::uint128;

    String data_file_name = MergeTreeDataPartCompact::DATA_FILE_NAME_WITH_EXTENSION;
    String marks_file_name = MergeTreeDataPartCompact::DATA_FILE_NAME +  marks_file_extension;

    checksums.files[data_file_name].is_compressed = true;
    size_t uncompressed_size = 0;
    uint128 uncompressed_hash{0, 0};

    for (const auto & [_, stream] : compressed_streams)
    {
        uncompressed_size += stream->hashing_buf.count();
        auto stream_hash = stream->hashing_buf.getHash();
        uncompressed_hash = CityHash_v1_0_2::CityHash128WithSeed(
            reinterpret_cast<char *>(&stream_hash), sizeof(stream_hash), uncompressed_hash);
    }

    checksums.files[data_file_name].uncompressed_size = uncompressed_size;
    checksums.files[data_file_name].uncompressed_hash = uncompressed_hash;
    checksums.files[data_file_name].file_size = plain_hashing.count();
    checksums.files[data_file_name].file_hash = plain_hashing.getHash();

    checksums.files[marks_file_name].file_size = marks.count();
    checksums.files[marks_file_name].file_hash = marks.getHash();
}

void MergeTreeDataPartWriterCompact::ColumnsBuffer::add(MutableColumns && columns)
{
    if (accumulated_columns.empty())
        accumulated_columns = std::move(columns);
    else
    {
        for (size_t i = 0; i < columns.size(); ++i)
            accumulated_columns[i]->insertRangeFrom(*columns[i], 0, columns[i]->size());
    }
}

Columns MergeTreeDataPartWriterCompact::ColumnsBuffer::releaseColumns()
{
    Columns res(std::make_move_iterator(accumulated_columns.begin()),
        std::make_move_iterator(accumulated_columns.end()));
    accumulated_columns.clear();
    return res;
}

size_t MergeTreeDataPartWriterCompact::ColumnsBuffer::size() const
{
    if (accumulated_columns.empty())
        return 0;
    return accumulated_columns.at(0)->size();
}

}
