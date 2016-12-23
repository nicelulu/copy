#include <DB/Storages/System/StorageSystemMerges.h>
#include <DB/DataTypes/DataTypeString.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/Columns/ColumnString.h>
#include <DB/DataStreams/OneBlockInputStream.h>
#include <DB/Interpreters/Context.h>
#include <DB/Storages/MergeTree/MergeList.h>


namespace DB
{

StorageSystemMerges::StorageSystemMerges(const std::string & name)
	: name{name}
	, columns{
		{ "database", 						std::make_shared<DataTypeString>() },
		{ "table",							std::make_shared<DataTypeString>() },
		{ "elapsed",						std::make_shared<DataTypeFloat64>() },
		{ "progress",						std::make_shared<DataTypeFloat64>() },
		{ "num_parts",						std::make_shared<DataTypeUInt64>() },
		{ "result_part_name",				std::make_shared<DataTypeString>() },
		{ "total_size_bytes_compressed",	std::make_shared<DataTypeUInt64>() },
		{ "total_size_marks",				std::make_shared<DataTypeUInt64>() },
		{ "bytes_read_uncompressed",		std::make_shared<DataTypeUInt64>() },
		{ "rows_read",						std::make_shared<DataTypeUInt64>() },
		{ "bytes_written_uncompressed", 	std::make_shared<DataTypeUInt64>() },
		{ "rows_written",					std::make_shared<DataTypeUInt64>() },
		{ "columns_written",				std::make_shared<DataTypeUInt64>() },
		{ "memory_usage",					std::make_shared<DataTypeUInt64>() },
		{ "thread_number",					std::make_shared<DataTypeUInt64>() },
	}
{
}

StoragePtr StorageSystemMerges::create(const std::string & name)
{
	return make_shared(name);
}

BlockInputStreams StorageSystemMerges::read(
	const Names & column_names,
	ASTPtr query,
	const Context & context,
	const Settings & settings,
	QueryProcessingStage::Enum & processed_stage,
	const size_t max_block_size,
	const unsigned)
{
	check(column_names);
	processed_stage = QueryProcessingStage::FetchColumns;

	Block block = getSampleBlock();

	for (const auto & merge : context.getMergeList().get())
	{
		size_t i = 0;
		block.unsafeGetByPosition(i++).column->insert(merge.database);
		block.unsafeGetByPosition(i++).column->insert(merge.table);
		block.unsafeGetByPosition(i++).column->insert(merge.elapsed);
		block.unsafeGetByPosition(i++).column->insert(std::min(1., merge.progress)); /// little cheat
		block.unsafeGetByPosition(i++).column->insert(merge.num_parts);
		block.unsafeGetByPosition(i++).column->insert(merge.result_part_name);
		block.unsafeGetByPosition(i++).column->insert(merge.total_size_bytes_compressed);
		block.unsafeGetByPosition(i++).column->insert(merge.total_size_marks);
		block.unsafeGetByPosition(i++).column->insert(merge.bytes_read_uncompressed);
		block.unsafeGetByPosition(i++).column->insert(merge.rows_read);
		block.unsafeGetByPosition(i++).column->insert(merge.bytes_written_uncompressed);
		block.unsafeGetByPosition(i++).column->insert(merge.rows_written);
		block.unsafeGetByPosition(i++).column->insert(merge.columns_written);
		block.unsafeGetByPosition(i++).column->insert(merge.memory_usage);
		block.unsafeGetByPosition(i++).column->insert(merge.thread_number);
	}

	return BlockInputStreams{1, std::make_shared<OneBlockInputStream>(block)};
}

}
