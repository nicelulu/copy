#include <Interpreters/IJoin.h>
#include <Columns/ColumnNullable.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <DataStreams/materializeBlock.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int TYPE_MISMATCH;
}


namespace JoinCommon
{

void convertColumnToNullable(ColumnWithTypeAndName & column)
{
    if (column.type->isNullable() || !column.type->canBeInsideNullable())
        return;

    column.type = makeNullable(column.type);
    if (column.column)
        column.column = makeNullable(column.column);
}

void convertColumnsToNullable(Block & block, size_t starting_pos)
{
    for (size_t i = starting_pos; i < block.columns(); ++i)
        convertColumnToNullable(block.getByPosition(i));
}

ColumnRawPtrs extractKeysForJoin(const Names & key_names_right, const Block & right_sample_block,
                                 Block & sample_block_with_keys, Block & sample_block_with_columns_to_add)
{
    size_t keys_size = key_names_right.size();
    ColumnRawPtrs key_columns(keys_size);

    sample_block_with_columns_to_add = materializeBlock(right_sample_block);

    for (size_t i = 0; i < keys_size; ++i)
    {
        const String & column_name = key_names_right[i];

        /// there could be the same key names
        if (sample_block_with_keys.has(column_name))
        {
            key_columns[i] = sample_block_with_keys.getByName(column_name).column.get();
            continue;
        }

        auto & col = sample_block_with_columns_to_add.getByName(column_name);
        col.column = recursiveRemoveLowCardinality(col.column);
        col.type = recursiveRemoveLowCardinality(col.type);

        /// Extract right keys with correct keys order.
        sample_block_with_keys.insert(col);
        sample_block_with_columns_to_add.erase(column_name);

        key_columns[i] = sample_block_with_keys.getColumns().back().get();

        /// We will join only keys, where all components are not NULL.
        if (auto * nullable = checkAndGetColumn<ColumnNullable>(*key_columns[i]))
            key_columns[i] = &nullable->getNestedColumn();
    }

    return key_columns;
}

void checkTypesOfKeys(const Block & block_left, const Names & key_names_left, const Block & block_right, const Names & key_names_right)
{
    size_t keys_size = key_names_left.size();

    for (size_t i = 0; i < keys_size; ++i)
    {
        DataTypePtr left_type = removeNullable(recursiveRemoveLowCardinality(block_left.getByName(key_names_left[i]).type));
        DataTypePtr right_type = removeNullable(recursiveRemoveLowCardinality(block_right.getByName(key_names_right[i]).type));

        if (!left_type->equals(*right_type))
            throw Exception("Type mismatch of columns to JOIN by: "
                + key_names_left[i] + " " + left_type->getName() + " at left, "
                + key_names_right[i] + " " + right_type->getName() + " at right",
                ErrorCodes::TYPE_MISMATCH);
    }
}

void createMissedColumns(Block & block)
{
    for (size_t i = 0; i < block.columns(); ++i)
    {
        auto & column = block.getByPosition(i);
        if (!column.column)
            column.column = column.type->createColumn();
    }
}

}
}
