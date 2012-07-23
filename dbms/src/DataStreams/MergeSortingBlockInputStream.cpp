#include <queue>

#include <DB/DataStreams/MergeSortingBlockInputStream.h>


namespace DB
{

Block MergeSortingBlockInputStream::readImpl()
{
	/** На данный момент - очень простой алгоритм:
	  * - прочитать в оперативку все блоки;
	  * - объединять по два соседних блока;
	  */

	if (has_been_read)
		return Block();
	
	has_been_read = true;

	Blocks blocks;
	while (Block block = input->read())
		blocks.push_back(block);

	if (blocks.empty())
		return Block();

	while (blocks.size() > 1)
	{
		for (Blocks::iterator it = blocks.begin(); it != blocks.end();)
		{
			Blocks::iterator next = it;
			++next;
			if (next == blocks.end())
				break;
			merge(*it, *next);
			++it;
			blocks.erase(it++);
		}
	}

	return blocks.front();
}


namespace
{
	typedef std::vector<const IColumn *> ConstColumnPlainPtrs;
	typedef std::vector<IColumn *> ColumnPlainPtrs;

/*	/// Курсор, позволяющий сравнивать соответствующие строки в разных блоках.
	struct Cursor
	{
		ConstColumns * all_columns;
		ConstColumns * sort_columns;
		size_t sort_columns_size;
		size_t pos;
		size_t rows;

		Cursor(ConstColumns * all_columns_, ConstColumns * sort_columns_, size_t pos_ = 0)
			: all_columns(all_columns_), sort_columns(sort_columns_), sort_columns_size(sort_columns->size()),
			pos(pos_), rows((*all_columns)[0].size()) {}

		bool operator< (const Cursor & rhs)
		{
			for (size_t i = 0; i < sort_columns_size; ++i)
			{
				int res = (*sort_columns)[i]->compareAt(pos, rhs.pos, (*rhs.sort_columns)[i]);
				if (res > 0)
					return true;
				if (res < 0)
					return false;
			}
			return false;
		}

		bool isLast() { return pos + 1 >= rows; }
		Cursor next() { return Cursor(all_columns, sort_columns, pos + 1); }
	};*/
}


/*Block MergeSortingBlockInputStream::merge(Blocks & blocks)
{
	Block merged;
	Columns merged_columns;

	if (!blocks.size())
		return merged;

	merged = blocks[0].cloneEmpty();

	typedef std::priority_queue<Cursor> Queue;
	Queue queue;

	size_t num_columns = blocks[0].columns();
	for (Blocks::const_iterator it = blocks.begin(); it != blocks.end(); ++it)
	{
		if (!*it)
			continue;

		for (size_t i = 0; i < num_columns; ++i)
		{
			

		}

		Cursor cursor;
	}

	for (size_t i = 0, size = description.size(); i < size; ++i)
	{
		size_t column_number = !description[i].column_name.empty()
			? left.getPositionByName(description[i].column_name)
			: description[i].column_number;

		left_sort_columns.push_back(&*left.getByPosition(column_number).column);
		right_sort_columns.push_back(&*right.getByPosition(column_number).column);
	}

	return merged;
}*/


void MergeSortingBlockInputStream::merge(Block & left, Block & right)
{
	Block merged = left.cloneEmpty();

	size_t left_size = left.rows();
	size_t right_size = right.rows();

	size_t left_pos = 0;
	size_t right_pos = 0;

	/// Все столбцы блоков.
	ConstColumnPlainPtrs left_columns;
	ConstColumnPlainPtrs right_columns;
	ColumnPlainPtrs merged_columns;

	/// Столбцы, по которым идёт сортировка.
	ConstColumnPlainPtrs left_sort_columns;
	ConstColumnPlainPtrs right_sort_columns;

	size_t num_columns = left.columns();
	for (size_t i = 0; i < num_columns; ++i)
	{
		left_columns.push_back(&*left.getByPosition(i).column);
		right_columns.push_back(&*right.getByPosition(i).column);
	}

	for (size_t i = 0, size = description.size(); i < size; ++i)
	{
		size_t column_number = !description[i].column_name.empty()
			? left.getPositionByName(description[i].column_name)
			: description[i].column_number;
		
		left_sort_columns.push_back(&*left.getByPosition(column_number).column);
		right_sort_columns.push_back(&*right.getByPosition(column_number).column);
	}

	/// Объединяем.
	while (right_pos < right_size || left_pos < left_size)
	{
		/// Откуда брать строку - из левого или из правого блока?
		int res = 0;
		if (right_pos == right_size)
			res = -1;
		else if (left_pos == left_size)
			res = 1;
		else
			for (size_t i = 0, size = description.size(); i < size; ++i)
				if ((res = description[i].direction * left_sort_columns[i]->compareAt(left_pos, right_pos, *right_sort_columns[i])))
					break;

		/// Вставляем строку в объединённый блок.
		if (res <= 0)
		{
			for (size_t i = 0; i < num_columns; ++i)
				merged_columns[i]->insert((*left_columns[i])[left_pos]);
			++left_pos;
		}
		else
		{
			for (size_t i = 0; i < num_columns; ++i)
				merged_columns[i]->insert((*right_columns[i])[right_pos]);
			++right_pos;
		}
	}

	left = merged;
}

}
