#pragma once

#include <Poco/SharedPtr.h>

#include <DB/Storages/IStorage.h>
#include <DB/DataStreams/IProfilingBlockInputStream.h>


namespace DB
{

using Poco::SharedPtr;


class NumbersBlockInputStream : public IProfilingBlockInputStream
{
public:
	NumbersBlockInputStream(size_t block_size_);
	Block readImpl();
	String getName() const { return "NumbersBlockInputStream"; }
private:
	size_t block_size;
	UInt64 next;
};


/** Реализует хранилище для системной таблицы Numbers.
  * Таблица содержит единственный столбец number UInt64.
  * Из этой таблицы можно прочитать все натуральные числа, начиная с 0 (до 2^64 - 1, а потом заново).
  */
class StorageSystemNumbers : public IStorage
{
public:
	StorageSystemNumbers(const std::string & name_);
	
	std::string getName() const { return "SystemNumbers"; }
	std::string getTableName() const { return "Numbers"; }

	const NamesAndTypes & getColumns() const { return columns; }

	BlockInputStreamPtr read(
		const Names & column_names,
		ASTPtr query,
		size_t max_block_size = DEFAULT_BLOCK_SIZE);

private:
	const std::string name;
	NamesAndTypes columns;
};

}
