#include <Storages/MergeTree/MergeTreeDataPartWriterInMemory.h>
#include <Storages/MergeTree/MergeTreeDataPartInMemory.h>
#include <Storages/MergeTree/MergeTreeWriteAheadLog.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

MergeTreeDataPartWriterInMemory::MergeTreeDataPartWriterInMemory(
    const DataPartInMemoryPtr & part_,
    const NamesAndTypesList & columns_list_,
    const MergeTreeWriterSettings & settings_)
    : IMergeTreeDataPartWriter(part_->storage, columns_list_, settings_)
    , part(part_) {}

void MergeTreeDataPartWriterInMemory::write(
    const Block & block, const IColumn::Permutation * permutation,
    const Block & primary_key_block, const Block & /* skip_indexes_block */)
{
    if (block_written)
        throw Exception("DataPartWriterInMemory supports only one write", ErrorCodes::LOGICAL_ERROR);

    Block result_block;
    if (permutation)
    {
        for (const auto & col : columns_list)
        {
            if (primary_key_block.has(col.name))
                result_block.insert(primary_key_block.getByName(col.name));
            else
            {
                auto permuted = block.getByName(col.name);
                permuted.column = permuted.column->permute(*permutation, 0);
                result_block.insert(permuted);
            }
        }
    }
    else
    {
        for (const auto & col : columns_list)
            result_block.insert(block.getByName(col.name));
    }

    part->block = std::move(result_block);
    block_written = true;
}

void MergeTreeDataPartWriterInMemory::calculateAndSerializePrimaryIndex(const Block & primary_index_block)
{
    size_t rows = primary_index_block.rows();
    if (!rows)
        return;

    index_granularity.appendMark(rows);
    if (with_final_mark)
        index_granularity.appendMark(0);

    size_t primary_columns_num = primary_index_block.columns();
    index_columns.resize(primary_columns_num);
    for (size_t i = 0; i < primary_columns_num; ++i)
    {
        const auto & primary_column = *primary_index_block.getByPosition(i).column;
        index_columns[i] = primary_column.cloneEmpty();
        index_columns[i]->insertFrom(primary_column, 0);
        if (with_final_mark)
            index_columns[i]->insertFrom(primary_column, rows - 1);
    }
}

static MergeTreeDataPartChecksum createUncompressedChecksum(size_t size, SipHash & hash)
{
    MergeTreeDataPartChecksum checksum;
    checksum.uncompressed_size = size;
    hash.get128(checksum.uncompressed_hash.first, checksum.uncompressed_hash.second);
    return checksum;
}

void MergeTreeDataPartWriterInMemory::finishDataSerialization(IMergeTreeDataPart::Checksums & checksums)
{
    SipHash hash;
    part->block.updateHash(hash);
    checksums.files["data.bin"] = createUncompressedChecksum(part->block.bytes(), hash);
}

}
