#pragma once

#include <DB/Dictionaries/IDictionarySource.h>
#include <DB/Dictionaries/MySQLBlockInputStream.h>
#include <statdaemons/ext/range.hpp>
#include <mysqlxx/Pool.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <strconvert/escape.h>

namespace DB
{

/// Allows loading dictionaries from a MySQL database
class MySQLDictionarySource final : public IDictionarySource
{
	static const auto max_block_size = 8192;

public:
	MySQLDictionarySource(const DictionaryStructure & dict_struct,
		const Poco::Util::AbstractConfiguration & config, const std::string & config_prefix,
		Block & sample_block)
		: dict_struct{dict_struct},
		  db{config.getString(config_prefix + ".db", "")},
		  table{config.getString(config_prefix + ".table")},
		  where{config.getString(config_prefix + ".where", "")},
		  sample_block{sample_block},
		  pool{config, config_prefix},
		  load_all_query{composeLoadAllQuery()},
		  last_modification{getLastModification()}
	{}

	/// copy-constructor is provided in order to support cloneability
	MySQLDictionarySource(const MySQLDictionarySource & other)
		: dict_struct{other.dict_struct},
		  db{other.db},
		  table{other.table},
		  where{other.where},
		  sample_block{other.sample_block},
		  pool{other.pool},
		  load_all_query{other.load_all_query}, last_modification{other.last_modification}
	{}

	BlockInputStreamPtr loadAll() override
	{
		last_modification = getLastModification();
		return new MySQLBlockInputStream{pool.Get(), load_all_query, sample_block, max_block_size};
	}

	BlockInputStreamPtr loadIds(const std::vector<std::uint64_t> & ids) override
	{
		last_modification = getLastModification();
		const auto query = composeLoadIdsQuery(ids);

		return new MySQLBlockInputStream{pool.Get(), query, sample_block, max_block_size};
	}

	bool isModified() const override { return getLastModification() > last_modification; }
	bool supportsSelectiveLoad() const override { return true; }

	DictionarySourcePtr clone() const override { return std::make_unique<MySQLDictionarySource>(*this); }

	std::string toString() const override
	{
		return "MySQL: " + db + '.' + table + (where.empty() ? "" : ", where: " + where);
	}

private:
	mysqlxx::DateTime getLastModification() const
	{
		const auto Update_time_idx = 12;
		mysqlxx::DateTime update_time{std::time(nullptr)};

		try
		{
			auto connection = pool.Get();
			auto query = connection->query("SHOW TABLE STATUS LIKE '%" + strconvert::escaped_for_like(table) + "%';");
			auto result = query.use();

			if (auto row = result.fetch())
			{
				const auto & update_time_value = row[Update_time_idx];

				if (!update_time_value.isNull())
					update_time = update_time_value.getDateTime();

				/// fetch remaining rows to avoid "commands out of sync" error
				while (auto row = result.fetch());
			}
		}
		catch (...)
		{
			tryLogCurrentException("MySQLDictionarySource");
		}

		/// we suppose failure to get modification time is not an error, therefore return current time
		return update_time;
	}

	std::string composeLoadAllQuery() const
	{
		std::string query;

		{
			WriteBufferFromString out{query};
			writeString("SELECT ", out);

			writeProbablyBackQuotedString(dict_struct.id_name, out);

			if (!dict_struct.range_min.empty() && !dict_struct.range_max.empty())
			{
				writeString(", ", out);
				writeProbablyBackQuotedString(dict_struct.range_min, out);
				writeString(", ", out);
				writeProbablyBackQuotedString(dict_struct.range_max, out);
			}

			for (const auto & attr : dict_struct.attributes)
			{
				writeString(", ", out);

				if (!attr.expression.empty())
				{
					writeString(attr.expression, out);
					writeString(" AS ", out);
				}

				writeProbablyBackQuotedString(attr.name, out);
			}

			writeString(" FROM ", out);
			if (!db.empty())
			{
				writeProbablyBackQuotedString(db, out);
				writeChar('.', out);
			}
			writeProbablyBackQuotedString(table, out);

			if (!where.empty())
			{
				writeString(" WHERE ", out);
				writeString(where, out);
			}

			writeChar(';', out);
		}

		return query;
	}

	std::string composeLoadIdsQuery(const std::vector<std::uint64_t> & ids)
	{
		std::string query;

		{
			WriteBufferFromString out{query};
			writeString("SELECT ", out);

			writeProbablyBackQuotedString(dict_struct.id_name, out);

			for (const auto & attr : dict_struct.attributes)
			{
				writeString(", ", out);

				if (!attr.expression.empty())
				{
					writeString(attr.expression, out);
					writeString(" AS ", out);
				}

				writeProbablyBackQuotedString(attr.name, out);
			}

			writeString(" FROM ", out);
			if (!db.empty())
			{
				writeProbablyBackQuotedString(db, out);
				writeChar('.', out);
			}
			writeProbablyBackQuotedString(table, out);

			writeString(" WHERE ", out);

			if (!where.empty())
			{
				writeString(where, out);
				writeString(" AND ", out);
			}

			writeProbablyBackQuotedString(dict_struct.id_name, out);
			writeString(" IN (", out);

			auto first = true;
			for (const auto id : ids)
			{
				if (!first)
					writeString(", ", out);

				first = false;
				writeString(DB::toString(id), out);
			}

			writeString(");", out);
		}

		return query;
	}

	const DictionaryStructure dict_struct;
	const std::string db;
	const std::string table;
	const std::string where;
	Block sample_block;
	mutable mysqlxx::PoolWithFailover pool;
	const std::string load_all_query;
	mysqlxx::DateTime last_modification;
};

}
