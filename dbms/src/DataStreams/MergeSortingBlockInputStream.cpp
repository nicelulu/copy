#include <DB/DataStreams/MergeSortingBlockInputStream.h>


namespace DB
{


Block MergeSortingBlockInputStream::readImpl()
{
	/** Достаточно простой алгоритм:
	  * - прочитать в оперативку все блоки;
	  * - объединить их всех;
	  */

	/// Ещё не прочитали блоки.
	if (!impl)
	{
		while (Block block = children.back()->read())
			blocks.push_back(block);

		if (blocks.empty() || isCancelled())
			return Block();

		impl.reset(new MergeSortingBlocksBlockInputStream(blocks, description, DEFAULT_BLOCK_SIZE, limit));
	}

	return impl->read();
}


MergeSortingBlocksBlockInputStream::MergeSortingBlocksBlockInputStream(
	Blocks & blocks_, SortDescription & description_, size_t max_merged_block_size_, size_t limit_)
	: blocks(blocks_), description(description_), max_merged_block_size(max_merged_block_size_), limit(limit_)
{
	Blocks nonempty_blocks;
	for (const auto & block : blocks)
	{
		if (block.rowsInFirstColumn() == 0)
			continue;

		nonempty_blocks.push_back(block);
		cursors.emplace_back(block, description);
		has_collation |= cursors.back().has_collation;
	}

	blocks.swap(nonempty_blocks);

	if (!has_collation)
	{
		for (size_t i = 0; i < cursors.size(); ++i)
			queue.push(SortCursor(&cursors[i]));
	}
	else
	{
		for (size_t i = 0; i < cursors.size(); ++i)
			queue_with_collation.push(SortCursorWithCollation(&cursors[i]));
	}
}


Block MergeSortingBlocksBlockInputStream::readImpl()
{
	if (blocks.empty())
		return Block();

	if (blocks.size() == 1)
	{
		Block res = blocks[0];
		blocks.clear();
		return res;
	}

	return !has_collation
		? mergeImpl<SortCursor>(queue)
		: mergeImpl<SortCursorWithCollation>(queue_with_collation);
}


template <typename TSortCursor>
Block MergeSortingBlocksBlockInputStream::mergeImpl(std::priority_queue<TSortCursor> & queue)
{
	Block merged = blocks[0].cloneEmpty();
	size_t num_columns = blocks[0].columns();

	ColumnPlainPtrs merged_columns;
	for (size_t i = 0; i < num_columns; ++i)	/// TODO: reserve
		merged_columns.push_back(merged.getByPosition(i).column.get());

	/// Вынимаем строки в нужном порядке и кладём в merged.
	size_t merged_rows = 0;
	while (!queue.empty())
	{
		TSortCursor current = queue.top();
		queue.pop();

		for (size_t i = 0; i < num_columns; ++i)
			merged_columns[i]->insertFrom(*current->all_columns[i], current->pos);

		if (!current->isLast())
		{
			current->next();
			queue.push(current);
		}

		++total_merged_rows;
		if (limit && total_merged_rows == limit)
		{
			blocks.clear();
			return merged;
		}

		++merged_rows;
		if (merged_rows == max_merged_block_size)
			return merged;
	}

	if (merged_rows == 0)
		merged.clear();

	return merged;
}


}
