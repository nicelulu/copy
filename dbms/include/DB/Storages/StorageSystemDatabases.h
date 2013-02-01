#pragma once

#include <Poco/SharedPtr.h>

#include <DB/Storages/IStorage.h>
#include <DB/DataStreams/IProfilingBlockInputStream.h>
#include <DB/Interpreters/Context.h>


namespace DB
{

using Poco::SharedPtr;


/** Реализует системную таблицу databases, которая позволяет получить информацию о всех БД.
  */
class StorageSystemDatabases : public IStorage
{
public:
	StorageSystemDatabases(const std::string & name_, const Context & context_);
	
	std::string getName() const { return "SystemDatabases"; }
	std::string getTableName() const { return name; }

	const NamesAndTypesList & getColumnsList() const { return columns; }

	BlockInputStreams read(
		const Names & column_names,
		ASTPtr query,
		const Settings & settings,
		QueryProcessingStage::Enum & processed_stage,
		size_t max_block_size = DEFAULT_BLOCK_SIZE,
		unsigned threads = 1);

private:
	const std::string name;
	const Context & context;
	NamesAndTypesList columns;
};

}
