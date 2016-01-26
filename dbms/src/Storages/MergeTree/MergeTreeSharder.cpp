#include <DB/Storages/MergeTree/MergeTreeSharder.h>
#include <DB/Storages/MergeTree/ReshardingJob.h>
#include <DB/Storages/MergeTree/MergedBlockOutputStream.h>
#include <DB/Common/escapeForFileName.h>
#include <DB/DataTypes/DataTypeArray.h>
#include <DB/IO/HashingWriteBuffer.h>
#include <DB/Common/BlockFilterCreator.h>
#include <DB/Interpreters/ExpressionAnalyzer.h>

#include <ctime>

namespace DB
{

namespace ErrorCodes
{
	extern const int LOGICAL_ERROR;
	extern const int TYPE_MISMATCH;
}

ShardedBlockWithDateInterval::ShardedBlockWithDateInterval(const Block & block_,
	size_t shard_no_, UInt16 min_date_, UInt16 max_date_)
	: block(block_), shard_no(shard_no_), min_date(min_date_), max_date(max_date_)
{
}

MergeTreeSharder::MergeTreeSharder(MergeTreeData & data_, const ReshardingJob & job_)
	: data(data_), job(job_), log(&Logger::get(data.getLogName() + " (Sharder)")),
	sharding_key_expr(ExpressionAnalyzer(job.sharding_key_expr, data.context, nullptr, data.getColumnsList()).getActions(false)),
	sharding_key_column_name(job.sharding_key_expr->getColumnName())
{
	for (size_t shard_no = 0; shard_no < job.paths.size(); ++shard_no)
	{
		const WeightedZooKeeperPath & weighted_path = job.paths[shard_no];
		slots.insert(slots.end(), weighted_path.second, shard_no);
	}
}

ShardedBlocksWithDateIntervals MergeTreeSharder::shardBlock(const Block & block)
{
	ShardedBlocksWithDateIntervals res;

	const auto num_cols = block.columns();

	/// cache column pointers for later reuse
	std::vector<const IColumn*> columns(num_cols);
	for (size_t i = 0; i < columns.size(); ++i)
		columns[i] = block.getByPosition(i).column;

	auto filters = createFilters(block);

	const auto num_shards = job.paths.size();

	ssize_t size_hint = ((block.rowsInFirstColumn() + num_shards - 1) / num_shards) * 1.1;	/// Число 1.1 выбрано наугад.

	for (size_t shard_no = 0; shard_no < num_shards; ++shard_no)
	{
		auto target_block = block.cloneEmpty();

		for (size_t col = 0; col < num_cols; ++col)
			target_block.getByPosition(col).column = columns[col]->filter(filters[shard_no], size_hint);

		if (target_block.rowsInFirstColumn())
		{
			/// Достаём столбец с датой.
			const ColumnUInt16::Container_t & dates =
				typeid_cast<const ColumnUInt16 &>(*target_block.getByName(data.date_column_name).column).getData();

			/// Минимальная и максимальная дата.
			UInt16 min_date = std::numeric_limits<UInt16>::max();
			UInt16 max_date = std::numeric_limits<UInt16>::min();
			for (ColumnUInt16::Container_t::const_iterator it = dates.begin(); it != dates.end(); ++it)
			{
				if (*it < min_date)
					min_date = *it;
				if (*it > max_date)
					max_date = *it;
			}

			res.emplace_back(target_block, shard_no, min_date, max_date);
		}
	}

	return res;
}

MergeTreeData::MutableDataPartPtr MergeTreeSharder::writeTempPart(
	ShardedBlockWithDateInterval & sharded_block_with_dates, Int64 temp_index)
{
	Block & block = sharded_block_with_dates.block;
	UInt16 min_date = sharded_block_with_dates.min_date;
	UInt16 max_date = sharded_block_with_dates.max_date;
	size_t shard_no = sharded_block_with_dates.shard_no;

	const auto & date_lut = DateLUT::instance();

	DayNum_t min_month = date_lut.toFirstDayNumOfMonth(DayNum_t(min_date));
	DayNum_t max_month = date_lut.toFirstDayNumOfMonth(DayNum_t(max_date));

	if (min_month != max_month)
		throw Exception("Logical error: part spans more than one month.", ErrorCodes::LOGICAL_ERROR);

	size_t part_size = (block.rows() + data.index_granularity - 1) / data.index_granularity;

	String tmp_part_name = "tmp_" + ActiveDataPartSet::getPartName(
		DayNum_t(min_date), DayNum_t(max_date),
		temp_index, temp_index, 0);

	String part_tmp_path = data.getFullPath() + "reshard/" + toString(shard_no) + "/" + tmp_part_name + "/";

	Poco::File(part_tmp_path).createDirectories();

	MergeTreeData::MutableDataPartPtr new_data_part = std::make_shared<MergeTreeData::DataPart>(data);
	new_data_part->name = tmp_part_name;
	new_data_part->is_temp = true;

	/// Если для сортировки надо вычислить некоторые столбцы - делаем это.
	if (data.mode != MergeTreeData::Unsorted)
		data.getPrimaryExpression()->execute(block);

	SortDescription sort_descr = data.getSortDescription();

	/// Сортируем.
	IColumn::Permutation * perm_ptr = nullptr;
	IColumn::Permutation perm;
	if (data.mode != MergeTreeData::Unsorted)
	{
		if (!isAlreadySorted(block, sort_descr))
		{
			stableGetPermutation(block, sort_descr, perm);
			perm_ptr = &perm;
		}
	}

	NamesAndTypesList columns = data.getColumnsList().filter(block.getColumnsList().getNames());
	MergedBlockOutputStream out(data, part_tmp_path, columns, CompressionMethod::LZ4);

	out.getIndex().reserve(part_size * sort_descr.size());

	out.writePrefix();
	out.writeWithPermutation(block, perm_ptr);
	MergeTreeData::DataPart::Checksums checksums = out.writeSuffixAndGetChecksums();

	new_data_part->left_date = DayNum_t(min_date);
	new_data_part->right_date = DayNum_t(max_date);
	new_data_part->left = temp_index;
	new_data_part->right = temp_index;
	new_data_part->level = 0;
	new_data_part->size = part_size;
	new_data_part->modification_time = std::time(0);
	new_data_part->month = min_month;
	new_data_part->columns = columns;
	new_data_part->checksums = checksums;
	new_data_part->index.swap(out.getIndex());
	new_data_part->size_in_bytes = MergeTreeData::DataPart::calcTotalSize(part_tmp_path);
	new_data_part->is_sharded = true;
	new_data_part->shard_no = sharded_block_with_dates.shard_no;

	return new_data_part;
}

std::vector<IColumn::Filter> MergeTreeSharder::createFilters(Block block)
{
	using create_filters_sig = std::vector<IColumn::Filter>(size_t, const IColumn *, size_t num_shards, const std::vector<size_t> & slots);
	/// hashmap of pointers to functions corresponding to each integral type
	static std::unordered_map<std::string, create_filters_sig *> creators{
		{ TypeName<UInt8>::get(), &BlockFilterCreator<UInt8>::perform },
		{ TypeName<UInt16>::get(), &BlockFilterCreator<UInt16>::perform },
		{ TypeName<UInt32>::get(), &BlockFilterCreator<UInt32>::perform },
		{ TypeName<UInt64>::get(), &BlockFilterCreator<UInt64>::perform },
		{ TypeName<Int8>::get(), &BlockFilterCreator<Int8>::perform },
		{ TypeName<Int16>::get(), &BlockFilterCreator<Int16>::perform },
		{ TypeName<Int32>::get(), &BlockFilterCreator<Int32>::perform },
		{ TypeName<Int64>::get(), &BlockFilterCreator<Int64>::perform },
	};

	sharding_key_expr->execute(block);

	const auto & key_column = block.getByName(sharding_key_column_name);

	/// check that key column has valid type
	const auto it = creators.find(key_column.type->getName());

	return it != std::end(creators)
		? (*it->second)(block.rowsInFirstColumn(), key_column.column.get(), job.paths.size(), slots)
		: throw Exception{
			"Sharding key expression does not evaluate to an integer type",
			ErrorCodes::TYPE_MISMATCH
		};
}

}
