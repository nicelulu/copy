#include <DB/DataStreams/CollapsingFinalBlockInputStream.h>

/// Максимальное количество сообщений о некорректных данных в логе.
#define MAX_ERROR_MESSAGES 10


namespace DB
{

CollapsingFinalBlockInputStream::~CollapsingFinalBlockInputStream()
{
	/// Нужно обезвредить все MergingBlockPtr, чтобы они не пытались класть блоки в output_blocks.
	previous.block.cancel();
	last_positive.block.cancel();
	
	while (!queue.empty())
	{
		Cursor c = queue.top();
		queue.pop();
		c.block.cancel();
	}
	
	for (size_t i = 0; i < output_blocks.size(); ++i)
		delete output_blocks[i];
}

void CollapsingFinalBlockInputStream::reportBadCounts()
{
	/// При неконсистентных данных, это - неизбежная ошибка, которая не может быть легко исправлена админами. Поэтому Warning.
	LOG_WARNING(log, "Incorrect data: number of rows with sign = 1 (" << count_positive
		<< ") differs with number of rows with sign = -1 (" << count_negative
		<< ") by more than one");
}

void CollapsingFinalBlockInputStream::reportBadSign(Int8 sign)
{
	LOG_ERROR(log, "Invalid sign: " << static_cast<int>(sign));
}

void CollapsingFinalBlockInputStream::fetchNextBlock(size_t input_index)
{
	BlockInputStreamPtr stream = children[input_index];
	Block block = stream->read();
	if (!block)
		return;
	MergingBlockPtr merging_block(new MergingBlock(block, input_index, description, sign_column, &output_blocks));
	++blocks_fetched;
	queue.push(Cursor(merging_block));
}

void CollapsingFinalBlockInputStream::commitCurrent()
{
	if (count_positive || count_negative)
	{
		if (count_positive >= count_negative)
		{
			last_positive.addToFilter();
		}
		
		if (!(count_positive == count_negative || count_positive + 1 == count_negative || count_positive == count_negative + 1))
		{
			if (count_incorrect_data < MAX_ERROR_MESSAGES)
				reportBadCounts();
			++count_incorrect_data;
		}
		
		last_positive = Cursor();
		previous = Cursor();
	}
	
	count_negative = 0;
	count_positive = 0;
}

Block CollapsingFinalBlockInputStream::readImpl()
{
	if (first)
	{
		for (size_t i = 0; i < children.size(); ++i)
			fetchNextBlock(i);

		first = false;
	}
	
	/// Будем формировать блоки для ответа, пока не получится непустой блок.
	while (true)
	{
		while (!queue.empty() && output_blocks.empty())
		{
			Cursor current = queue.top();
			queue.pop();
			
			bool has_next = !queue.empty();
			Cursor next = has_next ? queue.top() : Cursor();
			
			/// Будем продвигаться в текущем блоке, не используя очередь, пока возможно.
			while (true)
			{
				if (!current.equal(previous))
				{
					commitCurrent();
					previous = current;
				}
				
				Int8 sign = current.getSign();
				if (sign == 1)
				{
					last_positive = current;
					++count_positive;
				}
				else if (sign == -1)
				{
					++count_negative;
				}
				else
					reportBadSign(sign);
				
				if (current.isLast())
				{
					fetchNextBlock(current.block->stream_index);
					
					/// Все потоки кончились. Обработаем последний ключ.
					if (!has_next)
					{
						commitCurrent();
					}
					
					break;
				}
				else
				{
					current.next();
					
					if (has_next && !(next < current))
					{
						queue.push(current);
						
						break;
					}
				}
			}
		}
		
		/// Конец потока.
		if (output_blocks.empty())
		{
			if (blocks_fetched != blocks_output)
				LOG_ERROR(log, "Logical error: CollapsingFinalBlockInputStream has output " << blocks_output << " blocks instead of " << blocks_fetched);
			
			return Block();
		}
		
		MergingBlock * merging_block = output_blocks.back();
		Block block = merging_block->block;
		
		for (size_t i = 0; i < block.columns(); ++i)
			block.getByPosition(i).column = block.getByPosition(i).column->filter(merging_block->filter);
		
		output_blocks.pop_back();
		delete merging_block;
		
		++blocks_output;
		
		if (block)
			return block;
	}
}

}
