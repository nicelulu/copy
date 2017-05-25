#include <Dictionaries/DictionarySourceHelpers.h>
#include <Dictionaries/DictionaryStructure.h>
#include <Core/ColumnWithTypeAndName.h>
#include <Core/Block.h>
#include <Columns/ColumnsNumber.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataStreams/IBlockOutputStream.h>


namespace DB
{

/// For simple key
void formatIDs(BlockOutputStreamPtr & out, const std::vector<UInt64> & ids)
{
    auto column = std::make_shared<ColumnUInt64>(ids.size());
    memcpy(column->getData().data(), ids.data(), ids.size() * sizeof(ids.front()));

    Block block{{ std::move(column), std::make_shared<DataTypeUInt64>(), "id" }};

    out->writePrefix();
    out->write(block);
    out->writeSuffix();
    out->flush();
}

/// For composite key
void formatKeys(const DictionaryStructure & dict_struct, BlockOutputStreamPtr & out, const ConstColumnPlainPtrs & key_columns)
{
    Block block;

    for (size_t i = 0, size = key_columns.size(); i < size; ++i)
    {
        const auto & key_description = (*dict_struct.key)[i];
        const auto & key = key_columns[i];
        ColumnWithTypeAndName column;
        column.type = key_description.type;
        column.column = key->clone();   /// NOTE excessive copying
        block.insert(std::move(column));
    }

    out->writePrefix();
    out->write(block);
    out->writeSuffix();
    out->flush();
}

}
