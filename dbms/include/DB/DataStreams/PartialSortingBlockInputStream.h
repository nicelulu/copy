#pragma once

#include <DB/Core/SortDescription.h>

#include <DB/DataStreams/IProfilingBlockInputStream.h>


namespace DB
{

/** Сортирует каждый блок по отдельности по значениям указанных столбцов.
  * На данный момент, используется не очень оптимальный алгоритм.
  */
class PartialSortingBlockInputStream : public IProfilingBlockInputStream
{
public:
	PartialSortingBlockInputStream(BlockInputStreamPtr input_, SortDescription & description_)
		: input(input_), description(description_)
	{
		children.push_back(input);
	}

	Block readImpl();

	String getName() const { return "PartialSortingBlockInputStream"; }

private:
	BlockInputStreamPtr input;
	SortDescription description;
};

}
