#include <DB/Storages/StorageReplicatedMergeTree.h>
#include <DB/Storages/MergeTree/ReplicatedMergeTreeBlockOutputStream.h>
#include <DB/Storages/MergeTree/ReplicatedMergeTreePartsExchange.h>
#include <DB/Parsers/formatAST.h>
#include <DB/IO/WriteBufferFromOStream.h>
#include <DB/IO/ReadBufferFromString.h>

namespace DB
{

StorageReplicatedMergeTree::StorageReplicatedMergeTree(
	const String & zookeeper_path_,
	const String & replica_name_,
	bool attach,
	const String & path_, const String & name_, NamesAndTypesListPtr columns_,
	Context & context_,
	ASTPtr & primary_expr_ast_,
	const String & date_column_name_,
	const ASTPtr & sampling_expression_,
	size_t index_granularity_,
	MergeTreeData::Mode mode_,
	const String & sign_column_,
	const MergeTreeSettings & settings_)
	:
	context(context_), zookeeper(context.getZooKeeper()),
	path(path_), name(name_), full_path(path + escapeForFileName(name) + '/'), zookeeper_path(zookeeper_path_),
	replica_name(replica_name_),
	data(	full_path, columns_, context_, primary_expr_ast_, date_column_name_, sampling_expression_,
			index_granularity_,mode_, sign_column_, settings_),
	reader(data), writer(data), fetcher(data),
	log(&Logger::get("StorageReplicatedMergeTree")),
	shutdown_called(false)
{
	if (!zookeeper_path.empty() && *zookeeper_path.rbegin() == '/')
		zookeeper_path.erase(zookeeper_path.end() - 1);
	replica_path = zookeeper_path + "/replicas/" + replica_name;

	if (!attach)
	{
		if (!zookeeper.exists(zookeeper_path))
			createTable();

		if (!isTableEmpty())
			throw Exception("Can't add new replica to non-empty table", ErrorCodes::ADDING_REPLICA_TO_NON_EMPTY_TABLE);

		checkTableStructure();
		createReplica();
	}
	else
	{
		checkTableStructure();
		checkParts();
	}

	loadQueue();
	activateReplica();
}

StoragePtr StorageReplicatedMergeTree::create(
	const String & zookeeper_path_,
	const String & replica_name_,
	bool attach,
	const String & path_, const String & name_, NamesAndTypesListPtr columns_,
	Context & context_,
	ASTPtr & primary_expr_ast_,
	const String & date_column_name_,
	const ASTPtr & sampling_expression_,
	size_t index_granularity_,
	MergeTreeData::Mode mode_,
	const String & sign_column_,
	const MergeTreeSettings & settings_)
{
	StorageReplicatedMergeTree * res = new StorageReplicatedMergeTree(zookeeper_path_, replica_name_, attach,
		path_, name_, columns_, context_, primary_expr_ast_, date_column_name_, sampling_expression_,
		index_granularity_, mode_, sign_column_, settings_);
	StoragePtr res_ptr = res->thisPtr();
	String endpoint_name = "ReplicatedMergeTree:" + res->replica_path;
	InterserverIOEndpointPtr endpoint = new ReplicatedMergeTreePartsServer(res->data, res_ptr);
	res->endpoint_holder = new InterserverIOEndpointHolder(endpoint_name, endpoint, res->context.getInterserverIOHandler());
	return res_ptr;
}

static String formattedAST(const ASTPtr & ast)
{
	if (!ast)
		return "";
	std::stringstream ss;
	formatAST(*ast, ss, 0, false, true);
	return ss.str();
}

void StorageReplicatedMergeTree::createTable()
{
	zookeeper.create(zookeeper_path, "", zkutil::CreateMode::Persistent);

	/// Запишем метаданные таблицы, чтобы реплики могли сверять с ними свою локальную структуру таблицы.
	std::stringstream metadata;
	metadata << "metadata format version: 1" << std::endl;
	metadata << "date column: " << data.date_column_name << std::endl;
	metadata << "sampling expression: " << formattedAST(data.sampling_expression) << std::endl;
	metadata << "index granularity: " << data.index_granularity << std::endl;
	metadata << "mode: " << static_cast<int>(data.mode) << std::endl;
	metadata << "sign column: " << data.sign_column << std::endl;
	metadata << "primary key: " << formattedAST(data.primary_expr_ast) << std::endl;
	metadata << "columns:" << std::endl;
	WriteBufferFromOStream buf(metadata);
	for (auto & it : data.getColumnsList())
	{
		writeBackQuotedString(it.first, buf);
		writeChar(' ', buf);
		writeString(it.second->getName(), buf);
		writeChar('\n', buf);
	}
	buf.next();

	zookeeper.create(zookeeper_path + "/metadata", metadata.str(), zkutil::CreateMode::Persistent);

	/// Создадим нужные "директории".
	zookeeper.create(zookeeper_path + "/replicas", "", zkutil::CreateMode::Persistent);
	zookeeper.create(zookeeper_path + "/blocks", "", zkutil::CreateMode::Persistent);
	zookeeper.create(zookeeper_path + "/block-numbers", "", zkutil::CreateMode::Persistent);
	zookeeper.create(zookeeper_path + "/temp", "", zkutil::CreateMode::Persistent);
}

/** Проверить, что список столбцов и настройки таблицы совпадают с указанными в ZK (/metadata).
	* Если нет - бросить исключение.
	*/
void StorageReplicatedMergeTree::checkTableStructure()
{
	String metadata_str = zookeeper.get(zookeeper_path + "/metadata");
	ReadBufferFromString buf(metadata_str);
	assertString("metadata format version: 1", buf);
	assertString("\ndate column: ", buf);
	assertString(data.date_column_name, buf);
	assertString("\nsampling expression: ", buf);
	assertString(formattedAST(data.sampling_expression), buf);
	assertString("\nindex granularity: ", buf);
	assertString(toString(data.index_granularity), buf);
	assertString("\nmode: ", buf);
	assertString(toString(static_cast<int>(data.mode)), buf);
	assertString("\nsign column: ", buf);
	assertString(data.sign_column, buf);
	assertString("\nprimary key: ", buf);
	assertString(formattedAST(data.primary_expr_ast), buf);
	assertString("\ncolumns:\n", buf);
	for (auto & it : data.getColumnsList())
	{
		String name;
		readBackQuotedString(name, buf);
		if (name != it.first)
			throw Exception("Unexpected column name in ZooKeeper: expected " + it.first + ", found " + name,
				ErrorCodes::UNKNOWN_IDENTIFIER);
		assertString(" ", buf);
		assertString(it.second->getName(), buf);
		assertString("\n", buf);
	}
	assertEOF(buf);
}

void StorageReplicatedMergeTree::createReplica()
{
	zookeeper.create(replica_path, "", zkutil::CreateMode::Persistent);
	zookeeper.create(replica_path + "/host", "", zkutil::CreateMode::Persistent);
	zookeeper.create(replica_path + "/log", "", zkutil::CreateMode::Persistent);
	zookeeper.create(replica_path + "/log_pointers", "", zkutil::CreateMode::Persistent);
	zookeeper.create(replica_path + "/queue", "", zkutil::CreateMode::Persistent);
	zookeeper.create(replica_path + "/parts", "", zkutil::CreateMode::Persistent);
}

void StorageReplicatedMergeTree::activateReplica()
{
	std::stringstream host;
	host << "host: " << context.getInterserverIOHost() << std::endl;
	host << "port: " << context.getInterserverIOPort() << std::endl;

	/// Одновременно объявим, что эта реплика активна, и обновим хост.
	zkutil::Ops ops;
	ops.push_back(new zkutil::Op::Create(replica_path + "/is_active", "", zookeeper.getDefaultACL(), zkutil::CreateMode::Ephemeral));
	ops.push_back(new zkutil::Op::SetData(replica_path + "/host", host.str(), -1));
	zookeeper.multi(ops);

	replica_is_active_node = zkutil::EphemeralNodeHolder::existing(replica_path + "/is_active", zookeeper);
}

bool StorageReplicatedMergeTree::isTableEmpty()
{
	Strings replicas = zookeeper.getChildren(zookeeper_path + "/replicas");
	for (const auto & replica : replicas)
	{
		if (!zookeeper.getChildren(zookeeper_path + "/replicas/" + replica + "/parts").empty())
			return false;
	}
	return true;
}

void StorageReplicatedMergeTree::checkParts()
{
	Strings expected_parts_vec = zookeeper.getChildren(replica_path + "/parts");
	NameSet expected_parts(expected_parts_vec.begin(), expected_parts_vec.end());

	MergeTreeData::DataParts parts = data.getDataParts();

	MergeTreeData::DataPartsVector unexpected_parts;
	for (const auto & part : parts)
	{
		if (expected_parts.count(part->name))
		{
			expected_parts.erase(part->name);
		}
		else
		{
			unexpected_parts.push_back(part);
		}
	}

	if (!expected_parts.empty())
		throw Exception("Not found " + toString(expected_parts.size())
			+ " parts (including " + *expected_parts.begin() + ") in table " + data.getTableName(),
			ErrorCodes::NOT_FOUND_EXPECTED_DATA_PART);

	if (unexpected_parts.size() > 1)
		throw Exception("More than one unexpected part (including " + unexpected_parts[0]->name
			+ ") in table " + data.getTableName(),
			ErrorCodes::TOO_MANY_UNEXPECTED_DATA_PARTS);

	for (MergeTreeData::DataPartPtr part : unexpected_parts)
	{
		LOG_ERROR(log, "Unexpected part " << part->name << ". Renaming it to ignored_" + part->name);
		data.renameAndDetachPart(part, "ignored_");
	}
}

void StorageReplicatedMergeTree::loadQueue()
{
	Poco::ScopedLock<Poco::FastMutex> lock(queue_mutex);

	Strings children = zookeeper.getChildren(replica_path + "/queue");
	std::sort(children.begin(), children.end());
	for (const String & child : children)
	{
		String s = zookeeper.get(child);
		queue.push_back(LogEntry::parse(s));
	}
}

void StorageReplicatedMergeTree::pullLogsToQueue()
{
	Poco::ScopedLock<Poco::FastMutex> lock(queue_mutex);

	Strings replicas = zookeeper.getChildren(zookeeper_path + "/replicas");
	for (const String & replica : replicas)
	{
		String log_path = zookeeper_path + "/" + replica + "/log";

		String pointer_str;
		UInt64 pointer;

		if (zookeeper.tryGet(replica_path + "/log_pointers/" + replica, pointer_str))
		{
			pointer = Poco::NumberParser::parseUnsigned64(pointer_str);
		}
		else
		{
			/// Если у нас еще нет указателя на лог этой реплики, поставим указатель на первую запись в нем.
			Strings entries = zookeeper.getChildren(log_path);
			std::sort(entries.begin(), entries.end());
			pointer = entries.empty() ? 0 : Poco::NumberParser::parseUnsigned64(entries[0].substr(strlen("log-")));

			zookeeper.create(replica_path + "/log_pointers/" + replica, toString(pointer), zkutil::CreateMode::Persistent);
		}

		String entry_str;
		while (zookeeper.tryGet(log_path + "/log-" + toString(pointer), entry_str))
		{
			queue.push_back(LogEntry::parse(entry_str));

			/// Одновременно добавим запись в очередь и продвинем указатель на лог.
			zkutil::Ops ops;
			ops.push_back(new zkutil::Op::Create(
				replica_path + "/queue/queue-", entry_str, zookeeper.getDefaultACL(), zkutil::CreateMode::PersistentSequential));
			ops.push_back(new zkutil::Op::SetData(
				replica_path + "/log_pointers/" + replica, toString(pointer + 1), -1));
			zookeeper.multi(ops);

			++pointer;
		}
	}
}

void StorageReplicatedMergeTree::optimizeQueue()
{

}

void StorageReplicatedMergeTree::executeSomeQueueEntry() { throw Exception("Not implemented", ErrorCodes::NOT_IMPLEMENTED); }

bool StorageReplicatedMergeTree::tryExecute(const LogEntry & entry) { throw Exception("Not implemented", ErrorCodes::NOT_IMPLEMENTED); }

String StorageReplicatedMergeTree::findReplicaHavingPart(const String & part_name) { throw Exception("Not implemented", ErrorCodes::NOT_IMPLEMENTED); }

void StorageReplicatedMergeTree::getPart(const String & name, const String & replica_name) { throw Exception("Not implemented", ErrorCodes::NOT_IMPLEMENTED); }

void StorageReplicatedMergeTree::shutdown()
{
	if (shutdown_called)
		return;
	shutdown_called = true;
	replica_is_active_node = nullptr;
	endpoint_holder = nullptr;

	/// Кажется, чтобы был невозможен дедлок, тут придется дождаться удаления MyInterserverIOEndpoint.
}

StorageReplicatedMergeTree::~StorageReplicatedMergeTree()
{
	try
	{
		shutdown();
	}
	catch(...)
	{
		tryLogCurrentException("~StorageReplicatedMergeTree");
	}
}

BlockInputStreams StorageReplicatedMergeTree::read(
		const Names & column_names,
		ASTPtr query,
		const Settings & settings,
		QueryProcessingStage::Enum & processed_stage,
		size_t max_block_size,
		unsigned threads)
{
	return reader.read(column_names, query, settings, processed_stage, max_block_size, threads);
}

BlockOutputStreamPtr StorageReplicatedMergeTree::write(ASTPtr query)
{
	String insert_id;
	if (ASTInsertQuery * insert = dynamic_cast<ASTInsertQuery *>(&*query))
		insert_id = insert->insert_id;

	return new ReplicatedMergeTreeBlockOutputStream(*this, insert_id);
}

void StorageReplicatedMergeTree::drop()
{
	replica_is_active_node = nullptr;
	zookeeper.removeRecursive(replica_path);
	if (zookeeper.getChildren(zookeeper_path + "/replicas").empty())
		zookeeper.removeRecursive(zookeeper_path);
}

void StorageReplicatedMergeTree::LogEntry::writeText(WriteBuffer & out) const
{
	writeString("format version: 1\n", out);
	switch (type)
	{
		case GET_PART:
			writeString("get\n", out);
			writeString(new_part_name, out);
			break;
		case MERGE_PARTS:
			writeString("merge\n", out);
			for (const String & s : parts_to_merge)
			{
				writeString(s, out);
				writeString("\n", out);
			}
			writeString("into\n", out);
			writeString(new_part_name, out);
			break;
	}
	writeString("\n", out);
}

void StorageReplicatedMergeTree::LogEntry::readText(ReadBuffer & in)
{
	String type_str;

	assertString("format version: 1\n", in);
	readString(type_str, in);
	assertString("\n", in);

	if (type_str == "get")
	{
		type = GET_PART;
		readString(new_part_name, in);
	}
	else if (type_str == "merge")
	{
		type = MERGE_PARTS;
		while (true)
		{
			String s;
			readString(s, in);
			assertString("\n", in);
			if (s == "into")
				break;
			parts_to_merge.push_back(s);
		}
		readString(new_part_name, in);
	}
	assertString("\n", in);
}

}
