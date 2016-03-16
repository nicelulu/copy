#pragma once

#include <DB/Databases/IDatabase.h>


namespace DB
{

class DatabaseFactory
{
public:
	static DatabasePtr get(
		const String & engine_name,
		const String & database_name,
		const String & path,
		boost::threadpool::pool * thread_pool);	/// thread_pool, если не nullptr может использоваться для распараллеливания загрузки таблиц.
};

}
