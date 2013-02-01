#include <map>

#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>

#include <DB/Storages/StorageMemory.h>


namespace DB
{

using Poco::SharedPtr;


MemoryBlockInputStream::MemoryBlockInputStream(const Names & column_names_, BlocksList::iterator begin_, BlocksList::iterator end_, StoragePtr owned_storage)
	: IProfilingBlockInputStream(owned_storage), column_names(column_names_), begin(begin_), end(end_), it(begin)
{
}


Block MemoryBlockInputStream::readImpl()
{
	if (it == end)
		return Block();
	else
		return *it++;
}


MemoryBlockOutputStream::MemoryBlockOutputStream(StoragePtr owned_storage)
	: IBlockOutputStream(owned_storage), storage(reinterpret_cast<StorageMemory &>(owned_storage))
{
}


void MemoryBlockOutputStream::write(const Block & block)
{
	storage.check(block);
	Poco::ScopedLock<Poco::FastMutex> lock(storage.mutex);
	storage.data.push_back(block);
}


StorageMemory::StorageMemory(const std::string & name_, NamesAndTypesListPtr columns_)
	: name(name_), columns(columns_)
{
}


BlockInputStreams StorageMemory::read(
	const Names & column_names,
	ASTPtr query,
	const Settings & settings,
	QueryProcessingStage::Enum & processed_stage,
	size_t max_block_size,
	unsigned threads)
{
	check(column_names);
	processed_stage = QueryProcessingStage::FetchColumns;

	Poco::ScopedLock<Poco::FastMutex> lock(mutex);

	size_t size = data.size();
	
	if (threads > size)
		threads = size;

	BlockInputStreams res;

	for (size_t thread = 0; thread < threads; ++thread)
	{
		BlocksList::iterator begin = data.begin();
		BlocksList::iterator end = data.begin();

		std::advance(begin, thread * size / threads);
		std::advance(end, (thread + 1) * size / threads);
		
		res.push_back(new MemoryBlockInputStream(column_names, begin, end, thisPtr()));
	}
	
	return res;
}

	
BlockOutputStreamPtr StorageMemory::write(
	ASTPtr query)
{
	return new MemoryBlockOutputStream(thisPtr());
}


void StorageMemory::dropImpl()
{
	Poco::ScopedLock<Poco::FastMutex> lock(mutex);
	data.clear();
}

}
