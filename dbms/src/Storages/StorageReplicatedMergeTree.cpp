#include <DB/Storages/StorageReplicatedMergeTree.h>
#include <DB/Storages/MergeTree/ReplicatedMergeTreeBlockOutputStream.h>
#include <DB/Storages/MergeTree/ReplicatedMergeTreePartsExchange.h>
#include <DB/Storages/MergeTree/MergeTreePartChecker.h>
#include <DB/Parsers/formatAST.h>
#include <DB/IO/WriteBufferFromOStream.h>
#include <DB/IO/ReadBufferFromString.h>
#include <DB/Interpreters/InterpreterAlterQuery.h>
#include <DB/Common/VirtualColumnUtils.h>
#include <time.h>

namespace DB
{


const auto ERROR_SLEEP_MS = 1000;
const auto MERGE_SELECTING_SLEEP_MS = 5 * 1000;
const auto CLEANUP_SLEEP_MS = 30 * 1000;

const auto RESERVED_BLOCK_NUMBERS = 200;

/// Преобразовать число в строку формате суффиксов автоинкрементных нод в ZooKeeper.
static String padIndex(UInt64 index)
{
	String index_str = toString(index);
	while (index_str.size() < 10)
		index_str = '0' + index_str;
	return index_str;
}


/// Используется для проверки, выставили ли ноду is_active мы, или нет.
static String generateActiveNodeIdentifier()
{
	struct timespec times;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &times))
		throwFromErrno("Cannot clock_gettime.", ErrorCodes::CANNOT_CLOCK_GETTIME);
	return toString(times.tv_nsec + times.tv_sec + getpid());
}


StorageReplicatedMergeTree::StorageReplicatedMergeTree(
	const String & zookeeper_path_,
	const String & replica_name_,
	bool attach,
	const String & path_, const String & database_name_, const String & name_,
	NamesAndTypesListPtr columns_,
	Context & context_,
	ASTPtr & primary_expr_ast_,
	const String & date_column_name_,
	const ASTPtr & sampling_expression_,
	size_t index_granularity_,
	MergeTreeData::Mode mode_,
	const String & sign_column_,
	const MergeTreeSettings & settings_)
	:
	context(context_), zookeeper(context.getZooKeeper()), database_name(database_name_),
	table_name(name_), full_path(path_ + escapeForFileName(table_name) + '/'),
	zookeeper_path(context.getMacros().expand(zookeeper_path_)),
	replica_name(context.getMacros().expand(replica_name_)),
	data(	full_path, columns_, context_, primary_expr_ast_, date_column_name_, sampling_expression_,
			index_granularity_, mode_, sign_column_, settings_, database_name_ + "." + table_name, true,
			std::bind(&StorageReplicatedMergeTree::enqueuePartForCheck, this, std::placeholders::_1)),
	reader(data), writer(data), merger(data), fetcher(data),
	log(&Logger::get(database_name_ + "." + table_name + " (StorageReplicatedMergeTree)")),
	shutdown_event(false)
{
	if (!zookeeper_path.empty() && *zookeeper_path.rbegin() == '/')
		zookeeper_path.erase(zookeeper_path.end() - 1);
	replica_path = zookeeper_path + "/replicas/" + replica_name;

	bool skip_sanity_checks = false;

	if (zookeeper && zookeeper->exists(replica_path + "/flags/force_restore_data"))
	{
		skip_sanity_checks = true;
		zookeeper->remove(replica_path + "/flags/force_restore_data");

		LOG_WARNING(log, "Skipping the limits on severity of changes to data parts and columns (flag "
			<< replica_path << "/flags/force_restore_data).");
	}

	data.loadDataParts(skip_sanity_checks);

	if (!zookeeper)
	{
		if (!attach)
			throw Exception("Can't create replicated table without ZooKeeper", ErrorCodes::NO_ZOOKEEPER);

		goReadOnlyPermanently();
		return;
	}

	if (!attach)
	{
		createTableIfNotExists();

		checkTableStructure(false, false);
		createReplica();
	}
	else
	{
		checkTableStructure(skip_sanity_checks, true);
		checkParts(skip_sanity_checks);
	}

	initVirtualParts();
	loadQueue();

	String unreplicated_path = full_path + "unreplicated/";
	if (Poco::File(unreplicated_path).exists())
	{
		LOG_INFO(log, "Have unreplicated data");

		unreplicated_data.reset(new MergeTreeData(unreplicated_path, columns_, context_, primary_expr_ast_,
			date_column_name_, sampling_expression_, index_granularity_, mode_, sign_column_, settings_,
			database_name_ + "." + table_name + "[unreplicated]", false));

		unreplicated_data->loadDataParts(skip_sanity_checks);

		unreplicated_reader.reset(new MergeTreeDataSelectExecutor(*unreplicated_data));
		unreplicated_merger.reset(new MergeTreeDataMerger(*unreplicated_data));
	}

	/// Сгенерируем этому экземпляру случайный идентификатор.
	active_node_identifier = generateActiveNodeIdentifier();

	/// В этом потоке реплика будет активирована.
	restarting_thread = std::thread(&StorageReplicatedMergeTree::restartingThread, this);
}

StoragePtr StorageReplicatedMergeTree::create(
	const String & zookeeper_path_,
	const String & replica_name_,
	bool attach,
	const String & path_, const String & database_name_, const String & name_,
	NamesAndTypesListPtr columns_,
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
		path_, database_name_, name_, columns_, context_, primary_expr_ast_, date_column_name_, sampling_expression_,
		index_granularity_, mode_, sign_column_, settings_);
	StoragePtr res_ptr = res->thisPtr();
	if (!res->is_read_only)
	{
		String endpoint_name = "ReplicatedMergeTree:" + res->replica_path;
		InterserverIOEndpointPtr endpoint = new ReplicatedMergeTreePartsServer(res->data, *res);
		res->endpoint_holder = new InterserverIOEndpointHolder(endpoint_name, endpoint, res->context.getInterserverIOHandler());
	}
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

void StorageReplicatedMergeTree::createTableIfNotExists()
{
	if (zookeeper->exists(zookeeper_path))
		return;

	LOG_DEBUG(log, "Creating table " << zookeeper_path);

	zookeeper->createAncestors(zookeeper_path);

	/// Запишем метаданные таблицы, чтобы реплики могли сверять с ними параметры таблицы.
	std::stringstream metadata;
	metadata << "metadata format version: 1" << std::endl;
	metadata << "date column: " << data.date_column_name << std::endl;
	metadata << "sampling expression: " << formattedAST(data.sampling_expression) << std::endl;
	metadata << "index granularity: " << data.index_granularity << std::endl;
	metadata << "mode: " << static_cast<int>(data.mode) << std::endl;
	metadata << "sign column: " << data.sign_column << std::endl;
	metadata << "primary key: " << formattedAST(data.primary_expr_ast) << std::endl;

	zkutil::Ops ops;
	ops.push_back(new zkutil::Op::Create(zookeeper_path, "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/metadata", metadata.str(),
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/columns", data.getColumnsList().toString(),
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/log", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/blocks", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/block_numbers", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/nonincrement_block_numbers", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/leader_election", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/temp", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(zookeeper_path + "/replicas", "",
										 zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));

	auto code = zookeeper->tryMulti(ops);
	if (code != ZOK && code != ZNODEEXISTS)
		throw zkutil::KeeperException(code);
}

/** Проверить, что список столбцов и настройки таблицы совпадают с указанными в ZK (/metadata).
	* Если нет - бросить исключение.
	*/
void StorageReplicatedMergeTree::checkTableStructure(bool skip_sanity_checks, bool allow_alter)
{
	String metadata_str = zookeeper->get(zookeeper_path + "/metadata");
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
	/// NOTE: Можно сделать менее строгую проверку совпадения выражений, чтобы таблицы не ломались от небольших изменений
	///       в коде formatAST.
	assertString(formattedAST(data.primary_expr_ast), buf);
	assertString("\n", buf);
	assertEOF(buf);

	zkutil::Stat stat;
	auto columns = NamesAndTypesList::parse(zookeeper->get(zookeeper_path + "/columns", &stat), context.getDataTypeFactory());
	columns_version = stat.version;
	if (columns != data.getColumnsList())
	{
		if (allow_alter && (data.getColumnsList().sizeOfDifference(columns) <= 2 || skip_sanity_checks))
		{
			LOG_WARNING(log, "Table structure in ZooKeeper is a little different from local table structure. Assuming ALTER.");

			/// Без всяких блокировок, потому что таблица еще не создана.
			InterpreterAlterQuery::updateMetadata(database_name, table_name, columns, context);
			data.setColumnsList(columns);
		}
		else
		{
			throw Exception("Table structure in ZooKeeper is too different from local table structure.",
							ErrorCodes::INCOMPATIBLE_COLUMNS);
		}
	}
}

void StorageReplicatedMergeTree::createReplica()
{
	LOG_DEBUG(log, "Creating replica " << replica_path);

	/// Создадим пустую реплику. Ноду columns создадим в конце - будем использовать ее в качестве признака, что создание реплики завершено.
	zkutil::Ops ops;
	ops.push_back(new zkutil::Op::Create(replica_path, "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(replica_path + "/host", "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(replica_path + "/log_pointer", "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(replica_path + "/queue", "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(replica_path + "/parts", "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(replica_path + "/flags", "", zookeeper->getDefaultACL(), zkutil::CreateMode::Persistent));
	zookeeper->multi(ops);

	/** Нужно изменить данные ноды /replicas на что угодно, чтобы поток, удаляющий старые записи в логе,
	  *  споткнулся об это изменение и не удалил записи, которые мы еще не прочитали.
	  */
	zookeeper->set(zookeeper_path + "/replicas", "last added replica: " + replica_name);

	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");

	/** "Эталонная" реплика, у которой мы возьмем информацию о множестве кусков, очередь и указатель на лог.
	  * Возьмем случайную из реплик, созданных раньше этой.
	  */
	String source_replica;

	Stat stat;
	zookeeper->exists(replica_path, &stat);
	auto my_create_time = stat.czxid;

	std::random_shuffle(replicas.begin(), replicas.end());
	for (const String & replica : replicas)
	{
		if (!zookeeper->exists(zookeeper_path + "/replicas/" + replica, &stat))
			throw Exception("Replica " + zookeeper_path + "/replicas/" + replica + " was removed from right under our feet.",
							ErrorCodes::NO_SUCH_REPLICA);
		if (stat.czxid < my_create_time)
		{
			source_replica = replica;
			break;
		}
	}

	if (source_replica.empty())
	{
		LOG_INFO(log, "This is the first replica");
	}
	else
	{
		LOG_INFO(log, "Will mimic " << source_replica);

		String source_path = zookeeper_path + "/replicas/" + source_replica;

		/** Если эталонная реплика еще не до конца создана, подождем.
		  * NOTE: Если при ее создании что-то пошло не так, можем провисеть тут вечно.
		  *       Можно создавать на время создания эфемерную ноду, чтобы быть уверенным, что реплика создается, а не заброшена.
		  *       То же можно делать и для таблицы. Можно автоматически удалять ноду реплики/таблицы,
		  *        если видно, что она создана не до конца, а создающий ее умер.
		  */
		while (!zookeeper->exists(source_path + "/columns"))
		{
			LOG_INFO(log, "Waiting for replica " << source_path << " to be fully created");

			zkutil::EventPtr event = new Poco::Event;
			if (zookeeper->exists(source_path + "/columns", nullptr, event))
			{
				LOG_WARNING(log, "Oops, a watch has leaked");
				break;
			}

			event->wait();
		}

		/// Порядок следующих трех действий важен. Записи в логе могут продублироваться, но не могут потеряться.

		/// Скопируем у эталонной реплики ссылку на лог.
		zookeeper->set(replica_path + "/log_pointer", zookeeper->get(source_path + "/log_pointer"));

		/// Запомним очередь эталонной реплики.
		Strings source_queue_names = zookeeper->getChildren(source_path + "/queue");
		std::sort(source_queue_names.begin(), source_queue_names.end());
		Strings source_queue;
		for (const String & entry_name : source_queue_names)
		{
			String entry;
			if (!zookeeper->tryGet(source_path + "/queue/" + entry_name, entry))
				continue;
			source_queue.push_back(entry);
		}

		/// Добавим в очередь задания на получение всех активных кусков, которые есть у эталонной реплики.
		Strings parts = zookeeper->getChildren(source_path + "/parts");
		ActiveDataPartSet active_parts_set;
		for (const String & part : parts)
		{
			active_parts_set.add(part);
		}
		Strings active_parts = active_parts_set.getParts();
		for (const String & name : active_parts)
		{
			LogEntry log_entry;
			log_entry.type = LogEntry::GET_PART;
			log_entry.source_replica = "";
			log_entry.new_part_name = name;

			zookeeper->create(replica_path + "/queue/queue-", log_entry.toString(), zkutil::CreateMode::PersistentSequential);
		}
		LOG_DEBUG(log, "Queued " << active_parts.size() << " parts to be fetched");

		/// Добавим в очередь содержимое очереди эталонной реплики.
		for (const String & entry : source_queue)
		{
			zookeeper->create(replica_path + "/queue/queue-", entry, zkutil::CreateMode::PersistentSequential);
		}
		LOG_DEBUG(log, "Copied " << source_queue.size() << " queue entries");
	}

	zookeeper->create(replica_path + "/columns", data.getColumnsList().toString(), zkutil::CreateMode::Persistent);
}

void StorageReplicatedMergeTree::activateReplica()
{
	std::stringstream host;
	host << "host: " << context.getInterserverIOHost() << std::endl;
	host << "port: " << context.getInterserverIOPort() << std::endl;

	/** Если нода отмечена как активная, но отметка сделана в этом же экземпляре, удалим ее.
	  * Такое возможно только при истечении сессии в ZooKeeper.
	  * Здесь есть небольшой race condition (можем удалить не ту ноду, для которой сделали tryGet),
	  *  но он крайне маловероятен при нормальном использовании.
	  */
	String data;
	if (zookeeper->tryGet(replica_path + "/is_active", data) && data == active_node_identifier)
		zookeeper->tryRemove(replica_path + "/is_active");

	/// Одновременно объявим, что эта реплика активна, и обновим хост.
	zkutil::Ops ops;
	ops.push_back(new zkutil::Op::Create(replica_path + "/is_active",
		active_node_identifier, zookeeper->getDefaultACL(), zkutil::CreateMode::Ephemeral));
	ops.push_back(new zkutil::Op::SetData(replica_path + "/host", host.str(), -1));

	try
	{
		zookeeper->multi(ops);
	}
	catch (zkutil::KeeperException & e)
	{
		if (e.code == ZNODEEXISTS)
			throw Exception("Replica " + replica_path + " appears to be already active. If you're sure it's not, "
				"try again in a minute or remove znode " + replica_path + "/is_active manually", ErrorCodes::REPLICA_IS_ALREADY_ACTIVE);

		throw;
	}

	replica_is_active_node = zkutil::EphemeralNodeHolder::existing(replica_path + "/is_active", *zookeeper);
}

void StorageReplicatedMergeTree::checkParts(bool skip_sanity_checks)
{
	Strings expected_parts_vec = zookeeper->getChildren(replica_path + "/parts");

	/// Куски в ZK.
	NameSet expected_parts(expected_parts_vec.begin(), expected_parts_vec.end());

	MergeTreeData::DataParts parts = data.getAllDataParts();

	/// Локальные куски, которых нет в ZK.
	MergeTreeData::DataParts unexpected_parts;

	for (const auto & part : parts)
	{
		if (expected_parts.count(part->name))
		{
			expected_parts.erase(part->name);
		}
		else
		{
			unexpected_parts.insert(part);
		}
	}

	/// Какие локальные куски добавить в ZK.
	MergeTreeData::DataPartsVector parts_to_add;

	/// Какие куски нужно забрать с других реплик.
	Strings parts_to_fetch;

	for (const String & missing_name : expected_parts)
	{
		/// Если локально не хватает какого-то куска, но есть покрывающий его кусок, можно заменить в ZK недостающий покрывающим.
		auto containing = data.getActiveContainingPart(missing_name);
		if (containing)
		{
			LOG_ERROR(log, "Ignoring missing local part " << missing_name << " because part " << containing->name << " exists");
			if (unexpected_parts.count(containing))
			{
				parts_to_add.push_back(containing);
				unexpected_parts.erase(containing);
			}
		}
		else
		{
			LOG_ERROR(log, "Fetching missing part " << missing_name);
			parts_to_fetch.push_back(missing_name);
		}
	}

	for (const String & name : parts_to_fetch)
		expected_parts.erase(name);

	String sanity_report =
		"There are " + toString(unexpected_parts.size()) + " unexpected parts, "
					 + toString(parts_to_add.size()) + " unexpectedly merged parts, "
					 + toString(expected_parts.size()) + " missing obsolete parts, "
					 + toString(parts_to_fetch.size()) + " missing parts";
	bool insane =
		parts_to_add.size() > data.settings.replicated_max_unexpectedly_merged_parts ||
		unexpected_parts.size() > data.settings.replicated_max_unexpected_parts ||
		expected_parts.size() > data.settings.replicated_max_missing_obsolete_parts ||
		parts_to_fetch.size() > data.settings.replicated_max_missing_active_parts;

	if (insane && !skip_sanity_checks)
	{
		throw Exception("The local set of parts of table " + getTableName() + " doesn't look like the set of parts in ZooKeeper. "
			+ sanity_report,
			ErrorCodes::TOO_MANY_UNEXPECTED_DATA_PARTS);
	}

	if (insane)
	{
		LOG_WARNING(log, sanity_report);
	}

	/// Добавим в ZK информацию о кусках, покрывающих недостающие куски.
	for (MergeTreeData::DataPartPtr part : parts_to_add)
	{
		LOG_ERROR(log, "Adding unexpected local part to ZooKeeper: " << part->name);

		zkutil::Ops ops;
		checkPartAndAddToZooKeeper(part, ops);
		zookeeper->multi(ops);
	}

	/// Удалим из ZK информацию о кусках, покрытых только что добавленными.
	for (const String & name : expected_parts)
	{
		LOG_ERROR(log, "Removing unexpectedly merged local part from ZooKeeper: " << name);

		zkutil::Ops ops;
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name + "/columns", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name + "/checksums", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name, -1));
		zookeeper->multi(ops);
	}

	/// Добавим в очередь задание забрать недостающие куски с других реплик и уберем из ZK информацию, что они у нас есть.
	for (const String & name : parts_to_fetch)
	{
		LOG_ERROR(log, "Removing missing part from ZooKeeper and queueing a fetch: " << name);

		LogEntry log_entry;
		log_entry.type = LogEntry::GET_PART;
		log_entry.source_replica = "";
		log_entry.new_part_name = name;

		/// Полагаемся, что это происходит до загрузки очереди (loadQueue).
		zkutil::Ops ops;
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name + "/columns", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name + "/checksums", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + name, -1));
		ops.push_back(new zkutil::Op::Create(
			replica_path + "/queue/queue-", log_entry.toString(), zookeeper->getDefaultACL(), zkutil::CreateMode::PersistentSequential));
		zookeeper->multi(ops);
	}

	/// Удалим лишние локальные куски.
	for (MergeTreeData::DataPartPtr part : unexpected_parts)
	{
		LOG_ERROR(log, "Renaming unexpected part " << part->name << " to ignored_" + part->name);
		data.renameAndDetachPart(part, "ignored_", true);
	}
}

void StorageReplicatedMergeTree::initVirtualParts()
{
	auto parts = data.getDataParts();
	for (const auto & part : parts)
		virtual_parts.add(part->name);
}

void StorageReplicatedMergeTree::checkPartAndAddToZooKeeper(MergeTreeData::DataPartPtr part, zkutil::Ops & ops, String part_name)
{
	if (part_name.empty())
		part_name = part->name;

	check(part->columns);
	int expected_columns_version = columns_version;

	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");
	std::random_shuffle(replicas.begin(), replicas.end());
	String expected_columns_str = part->columns.toString();

	for (const String & replica : replicas)
	{
		zkutil::Stat stat_before, stat_after;
		String columns_str;
		if (!zookeeper->tryGet(zookeeper_path + "/replicas/" + replica + "/parts/" + part_name + "/columns", columns_str, &stat_before))
			continue;
		if (columns_str != expected_columns_str)
		{
			LOG_INFO(log, "Not checking checksums of part " << part_name << " with replica " << replica
				<< " because columns are different");
			continue;
		}
		String checksums_str;
		/// Проверим, что версия ноды со столбцами не изменилась, пока мы читали checksums.
		/// Это гарантирует, что столбцы и чексуммы относятся к одним и тем же данным.
		if (!zookeeper->tryGet(zookeeper_path + "/replicas/" + replica + "/parts/" + part_name + "/checksums", checksums_str) ||
			!zookeeper->exists(zookeeper_path + "/replicas/" + replica + "/parts/" + part_name + "/columns", &stat_after) ||
			stat_before.version != stat_after.version)
		{
			LOG_INFO(log, "Not checking checksums of part " << part_name << " with replica " << replica
				<< " because part changed while we were reading its checksums");
			continue;
		}

		auto checksums = MergeTreeData::DataPart::Checksums::parse(checksums_str);
		checksums.checkEqual(part->checksums, true);
	}

	if (zookeeper->exists(replica_path + "/parts/" + part_name))
	{
		LOG_ERROR(log, "checkPartAndAddToZooKeeper: node " << replica_path + "/parts/" + part_name << " already exists");
		return;
	}

	ops.push_back(new zkutil::Op::Check(
		zookeeper_path + "/columns",
		expected_columns_version));
	ops.push_back(new zkutil::Op::Create(
		replica_path + "/parts/" + part_name,
		"",
		zookeeper->getDefaultACL(),
		zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(
		replica_path + "/parts/" + part_name + "/columns",
		part->columns.toString(),
		zookeeper->getDefaultACL(),
		zkutil::CreateMode::Persistent));
	ops.push_back(new zkutil::Op::Create(
		replica_path + "/parts/" + part_name + "/checksums",
		part->checksums.toString(),
		zookeeper->getDefaultACL(),
		zkutil::CreateMode::Persistent));
}

void StorageReplicatedMergeTree::clearOldParts()
{
	auto table_lock = lockStructure(false);

	MergeTreeData::DataPartsVector parts = data.grabOldParts();
	size_t count = parts.size();

	if (!count)
		return;

	try
	{
		while (!parts.empty())
		{
			MergeTreeData::DataPartPtr part = parts.back();

			LOG_DEBUG(log, "Removing " << part->name);

			zkutil::Ops ops;
			ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name + "/columns", -1));
			ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name + "/checksums", -1));
			ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name, -1));
			auto code = zookeeper->tryMulti(ops);
			if (code != ZOK)
				LOG_WARNING(log, "Couldn't remove " << part->name << " from ZooKeeper: " << zkutil::ZooKeeper::error2string(code));

			part->remove();
			parts.pop_back();
		}
	}
	catch (...)
	{
		data.addOldParts(parts);
		throw;
	}

	LOG_DEBUG(log, "Removed " << count << " old parts");
}

void StorageReplicatedMergeTree::clearOldLogs()
{
	zkutil::Stat stat;
	if (!zookeeper->exists(zookeeper_path + "/log", &stat))
		throw Exception(zookeeper_path + "/log doesn't exist", ErrorCodes::NOT_FOUND_NODE);

	int children_count = stat.numChildren;

	/// Будем ждать, пока накопятся в 1.1 раза больше записей, чем нужно.
	if (static_cast<double>(children_count) < data.settings.replicated_logs_to_keep * 1.1)
		return;

	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas", &stat);
	UInt64 min_pointer = std::numeric_limits<UInt64>::max();
	for (const String & replica : replicas)
	{
		String pointer = zookeeper->get(zookeeper_path + "/replicas/" + replica + "/log_pointer");
		if (pointer.empty())
			return;
		min_pointer = std::min(min_pointer, parse<UInt64>(pointer));
	}

	Strings entries = zookeeper->getChildren(zookeeper_path + "/log");
	std::sort(entries.begin(), entries.end());

	/// Не будем трогать последние replicated_logs_to_keep записей.
	entries.erase(entries.end() - std::min(entries.size(), data.settings.replicated_logs_to_keep), entries.end());
	/// Не будем трогать записи, не меньшие min_pointer.
	entries.erase(std::lower_bound(entries.begin(), entries.end(), "log-" + padIndex(min_pointer)), entries.end());

	if (entries.empty())
		return;

	zkutil::Ops ops;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		ops.push_back(new zkutil::Op::Remove(zookeeper_path + "/log/" + entries[i], -1));

		if (ops.size() > 400 || i + 1 == entries.size())
		{
			/// Одновременно с очисткой лога проверим, не добавилась ли реплика с тех пор, как мы получили список реплик.
			ops.push_back(new zkutil::Op::Check(zookeeper_path + "/replicas", stat.version));
			zookeeper->multi(ops);
			ops.clear();
		}
	}

	LOG_DEBUG(log, "Removed " << entries.size() << " old log entries: " << entries.front() << " - " << entries.back());
}

void StorageReplicatedMergeTree::clearOldBlocks()
{
	zkutil::Stat stat;
	if (!zookeeper->exists(zookeeper_path + "/blocks", &stat))
		throw Exception(zookeeper_path + "/blocks doesn't exist", ErrorCodes::NOT_FOUND_NODE);

	int children_count = stat.numChildren;

	/// Чтобы делать "асимптотически" меньше запросов exists, будем ждать, пока накопятся в 1.1 раза больше блоков, чем нужно.
	if (static_cast<double>(children_count) < data.settings.replicated_deduplication_window * 1.1)
		return;

	LOG_TRACE(log, "Clearing about " << static_cast<size_t>(children_count) - data.settings.replicated_deduplication_window
		<< " old blocks from ZooKeeper. This might take several minutes.");

	Strings blocks = zookeeper->getChildren(zookeeper_path + "/blocks");

	std::vector<std::pair<Int64, String> > timed_blocks;

	for (const String & block : blocks)
	{
		zkutil::Stat stat;
		zookeeper->exists(zookeeper_path + "/blocks/" + block, &stat);
		timed_blocks.push_back(std::make_pair(stat.czxid, block));
	}

	zkutil::Ops ops;
	std::sort(timed_blocks.begin(), timed_blocks.end(), std::greater<std::pair<Int64, String>>());
	for (size_t i = data.settings.replicated_deduplication_window; i <  timed_blocks.size(); ++i)
	{
		ops.push_back(new zkutil::Op::Remove(zookeeper_path + "/blocks/" + timed_blocks[i].second + "/number", -1));
		ops.push_back(new zkutil::Op::Remove(zookeeper_path + "/blocks/" + timed_blocks[i].second + "/columns", -1));
		ops.push_back(new zkutil::Op::Remove(zookeeper_path + "/blocks/" + timed_blocks[i].second + "/checksums", -1));
		ops.push_back(new zkutil::Op::Remove(zookeeper_path + "/blocks/" + timed_blocks[i].second, -1));
		if (ops.size() > 400 || i + 1 == timed_blocks.size())
		{
			zookeeper->multi(ops);
			ops.clear();
		}
	}

	LOG_TRACE(log, "Cleared " << blocks.size() - data.settings.replicated_deduplication_window << " old blocks from ZooKeeper");
}

void StorageReplicatedMergeTree::loadQueue()
{
	std::unique_lock<std::mutex> lock(queue_mutex);

	Strings children = zookeeper->getChildren(replica_path + "/queue");
	std::sort(children.begin(), children.end());
	for (const String & child : children)
	{
		String s = zookeeper->get(replica_path + "/queue/" + child);
		LogEntryPtr entry = LogEntry::parse(s);
		entry->znode_name = child;
		entry->addResultToVirtualParts(*this);
		queue.push_back(entry);
	}
}

void StorageReplicatedMergeTree::pullLogsToQueue(zkutil::EventPtr next_update_event)
{
	std::unique_lock<std::mutex> lock(queue_mutex);

	String index_str = zookeeper->get(replica_path + "/log_pointer");
	UInt64 index;

	if (index_str.empty())
	{
		/// Если у нас еще нет указателя на лог, поставим указатель на первую запись в нем.
		Strings entries = zookeeper->getChildren(zookeeper_path + "/log");
		index = entries.empty() ? 0 : parse<UInt64>(std::min_element(entries.begin(), entries.end())->substr(strlen("log-")));

		zookeeper->set(replica_path + "/log_pointer", toString(index));
	}
	else
	{
		index = parse<UInt64>(index_str);
	}

	UInt64 first_index = index;

	size_t count = 0;
	String entry_str;
	while (zookeeper->tryGet(zookeeper_path + "/log/log-" + padIndex(index), entry_str))
	{
		++count;
		++index;

		LogEntryPtr entry = LogEntry::parse(entry_str);

		/// Одновременно добавим запись в очередь и продвинем указатель на лог.
		zkutil::Ops ops;
		ops.push_back(new zkutil::Op::Create(
			replica_path + "/queue/queue-", entry_str, zookeeper->getDefaultACL(), zkutil::CreateMode::PersistentSequential));
		ops.push_back(new zkutil::Op::SetData(
			replica_path + "/log_pointer", toString(index), -1));
		auto results = zookeeper->multi(ops);

		String path_created = dynamic_cast<zkutil::Op::Create &>(ops[0]).getPathCreated();
		entry->znode_name = path_created.substr(path_created.find_last_of('/') + 1);
		entry->addResultToVirtualParts(*this);
		queue.push_back(entry);
	}

	if (next_update_event)
	{
		if (zookeeper->exists(zookeeper_path + "/log/log-" + padIndex(index), nullptr, next_update_event))
			next_update_event->set();
	}

	if (!count)
		return;

	if (queue_task_handle)
		queue_task_handle->wake();

	LOG_DEBUG(log, "Pulled " << count << " entries to queue: log-" << padIndex(first_index) << " - log-" << padIndex(index - 1));
}

bool StorageReplicatedMergeTree::shouldExecuteLogEntry(const LogEntry & entry)
{
	if ((entry.type == LogEntry::MERGE_PARTS || entry.type == LogEntry::GET_PART || entry.type == LogEntry::ATTACH_PART)
		&& future_parts.count(entry.new_part_name))
	{
		LOG_DEBUG(log, "Not executing log entry for part " << entry.new_part_name <<
			" because another log entry for the same part is being processed. This shouldn't happen often.");
		return false;
	}

	if (entry.type == LogEntry::MERGE_PARTS)
	{
		/** Если какая-то из нужных частей сейчас передается или мерджится, подождем окончания этой операции.
		  * Иначе, даже если всех нужных частей для мерджа нет, нужно попытаться сделать мердж.
		  * Если каких-то частей не хватает, вместо мерджа будет попытка скачать кусок.
		  * Такая ситуация возможна, если получение какого-то куска пофейлилось, и его переместили в конец очереди.
		  */
		for (const auto & name : entry.parts_to_merge)
		{
			if (future_parts.count(name))
			{
				LOG_TRACE(log, "Not merging into part " << entry.new_part_name << " because part " << name << " is not ready yet.");
				return false;
			}
		}
	}

	return true;
}

bool StorageReplicatedMergeTree::executeLogEntry(const LogEntry & entry, BackgroundProcessingPool::Context & pool_context)
{
	if (entry.type == LogEntry::DROP_RANGE)
	{
		executeDropRange(entry);
		return true;
	}

	if (entry.type == LogEntry::GET_PART ||
		entry.type == LogEntry::MERGE_PARTS ||
		entry.type == LogEntry::ATTACH_PART)
	{
		/// Если у нас уже есть этот кусок или покрывающий его кусок, ничего делать не нужно.
		MergeTreeData::DataPartPtr containing_part = data.getActiveContainingPart(entry.new_part_name);

		/// Даже если кусок есть локально, его (в исключительных случаях) может не быть в zookeeper.
		if (containing_part && zookeeper->exists(replica_path + "/parts/" + containing_part->name))
		{
			if (!(entry.type == LogEntry::GET_PART && entry.source_replica == replica_name))
				LOG_DEBUG(log, "Skipping action for part " + entry.new_part_name + " - part already exists");
			return true;
		}
	}

	if (entry.type == LogEntry::GET_PART && entry.source_replica == replica_name)
		LOG_WARNING(log, "Part " << entry.new_part_name << " from own log doesn't exist.");

	bool do_fetch = false;

	if (entry.type == LogEntry::GET_PART)
	{
		do_fetch = true;
	}
	else if (entry.type == LogEntry::ATTACH_PART)
	{
		do_fetch = !executeAttachPart(entry);
	}
	else if (entry.type == LogEntry::MERGE_PARTS)
	{
		MergeTreeData::DataPartsVector parts;
		bool have_all_parts = true;
		for (const String & name : entry.parts_to_merge)
		{
			MergeTreeData::DataPartPtr part = data.getActiveContainingPart(name);
			if (!part)
			{
				have_all_parts = false;
				break;
			}
			if (part->name != name)
			{
				LOG_WARNING(log, "Part " << name << " is covered by " << part->name
					<< " but should be merged into " << entry.new_part_name << ". This shouldn't happen often.");
				have_all_parts = false;
				break;
			}
			parts.push_back(part);
		}

		if (!have_all_parts)
		{
			/// Если нет всех нужных кусков, попробуем взять у кого-нибудь уже помердженный кусок.
			do_fetch = true;
			LOG_DEBUG(log, "Don't have all parts for merge " << entry.new_part_name << "; will try to fetch it instead");
		}
		else
		{
			/// Если собираемся сливать большие куски, увеличим счетчик потоков, сливающих большие куски.
			for (const auto & part : parts)
			{
				if (part->size_in_bytes > data.settings.max_bytes_to_merge_parts_small)
				{
					pool_context.incrementCounter("big merges");
					pool_context.incrementCounter("replicated big merges");
					break;
				}
			}

			auto table_lock = lockStructure(false);

			MergeTreeData::Transaction transaction;
			MergeTreeData::DataPartPtr part = merger.mergeParts(parts, entry.new_part_name, &transaction);

			zkutil::Ops ops;
			checkPartAndAddToZooKeeper(part, ops);

			/** TODO: Переименование нового куска лучше делать здесь, а не пятью строчками выше,
			  *  чтобы оно было как можно ближе к zookeeper->multi.
			  */

			zookeeper->multi(ops);

			/** При ZCONNECTIONLOSS или ZOPERATIONTIMEOUT можем зря откатить локальные изменения кусков.
			  * Это не проблема, потому что в таком случае слияние останется в очереди, и мы попробуем снова.
			  */
			transaction.commit();
			merge_selecting_event.set();

			ProfileEvents::increment(ProfileEvents::ReplicatedPartMerges);
		}
	}
	else
	{
		throw Exception("Unexpected log entry type: " + toString(static_cast<int>(entry.type)));
	}

	if (do_fetch)
	{
		String replica;

		try
		{
			replica = findReplicaHavingPart(entry.new_part_name, true);
			if (replica.empty())
			{
				ProfileEvents::increment(ProfileEvents::ReplicatedPartFailedFetches);
				throw Exception("No active replica has part " + entry.new_part_name, ErrorCodes::NO_REPLICA_HAS_PART);
			}
			fetchPart(entry.new_part_name, replica);

			if (entry.type == LogEntry::MERGE_PARTS)
				ProfileEvents::increment(ProfileEvents::ReplicatedPartFetchesOfMerged);
		}
		catch (...)
		{
			/** Если не получилось скачать кусок, нужный для какого-то мерджа, лучше не пытаться получить другие куски для этого мерджа,
			  * а попытаться сразу получить помердженный кусок. Чтобы так получилось, переместим действия для получения остальных кусков
			  * для этого мерджа в конец очереди.
			  */
			try
			{
				std::unique_lock<std::mutex> lock(queue_mutex);

				/// Найдем действие по объединению этого куска с другими. Запомним других.
				StringSet parts_for_merge;
				LogEntries::iterator merge_entry;
				for (LogEntries::iterator it = queue.begin(); it != queue.end(); ++it)
				{
					if ((*it)->type == LogEntry::MERGE_PARTS)
					{
						if (std::find((*it)->parts_to_merge.begin(), (*it)->parts_to_merge.end(), entry.new_part_name)
							!= (*it)->parts_to_merge.end())
						{
							parts_for_merge = StringSet((*it)->parts_to_merge.begin(), (*it)->parts_to_merge.end());
							merge_entry = it;
							break;
						}
					}
				}

				if (!parts_for_merge.empty())
				{
					/// Переместим в конец очереди действия, получающие parts_for_merge.
					for (LogEntries::iterator it = queue.begin(); it != queue.end();)
					{
						auto it0 = it;
						++it;

						if (it0 == merge_entry)
							break;

						if (((*it0)->type == LogEntry::MERGE_PARTS || (*it0)->type == LogEntry::GET_PART)
							&& parts_for_merge.count((*it0)->new_part_name))
						{
							queue.splice(queue.end(), queue, it0, it);
						}
					}

					/** Если этого куска ни у кого нет, но в очереди упоминается мердж с его участием, то наверно этот кусок такой старый,
					  *  что его все померджили и удалили. Не будем бросать исключение, чтобы queueTask лишний раз не спала.
					  */
					if (replica.empty())
					{
						LOG_INFO(log, "No replica has part " << entry.new_part_name << ". Will fetch merged part instead.");
						return false;
					}
				}

				/// Если ни у кого нет куска, и в очереди нет слияний с его участием, проверим, есть ли у кого-то покрывающий его.
				if (replica.empty())
					enqueuePartForCheck(entry.new_part_name);
			}
			catch (...)
			{
				tryLogCurrentException(__PRETTY_FUNCTION__);
			}

			throw;
		}
	}

	return true;
}

void StorageReplicatedMergeTree::executeDropRange(const StorageReplicatedMergeTree::LogEntry & entry)
{
	LOG_INFO(log, (entry.detach ? "Detaching" : "Removing") << " parts inside " << entry.new_part_name << ".");

	{
		LogEntries to_wait;
		size_t removed_entries = 0;

		/// Удалим из очереди операции с кусками, содержащимися в удаляемом диапазоне.
		std::unique_lock<std::mutex> lock(queue_mutex);
		for (LogEntries::iterator it = queue.begin(); it != queue.end();)
		{
			if (((*it)->type == LogEntry::GET_PART || (*it)->type == LogEntry::MERGE_PARTS) &&
				ActiveDataPartSet::contains(entry.new_part_name, (*it)->new_part_name))
			{
				if ((*it)->currently_executing)
					to_wait.push_back(*it);
				auto code = zookeeper->tryRemove(replica_path + "/queue/" + (*it)->znode_name);
				if (code != ZOK)
					LOG_INFO(log, "Couldn't remove " << replica_path + "/queue/" + (*it)->znode_name << ": "
						<< zkutil::ZooKeeper::error2string(code));
				queue.erase(it++);
				++removed_entries;
			}
			else
				++it;
		}

		LOG_DEBUG(log, "Removed " << removed_entries << " entries from queue. "
			"Waiting for " << to_wait.size() << " entries that are currently executing.");

		/// Дождемся завершения операций с кусками, содержащимися в удаляемом диапазоне.
		for (LogEntryPtr & entry : to_wait)
			entry->execution_complete.wait(lock, [&entry] { return !entry->currently_executing; });
	}

	LOG_DEBUG(log, (entry.detach ? "Detaching" : "Removing") << " parts.");
	size_t removed_parts = 0;

	/// Удалим куски, содержащиеся в удаляемом диапазоне.
	auto parts = data.getDataParts();
	for (const auto & part : parts)
	{
		if (!ActiveDataPartSet::contains(entry.new_part_name, part->name))
			continue;
		LOG_DEBUG(log, "Removing part " << part->name);
		++removed_parts;

		/// Если кусок удалять не нужно, надежнее переместить директорию до изменений в ZooKeeper.
		if (entry.detach)
			data.renameAndDetachPart(part);

		zkutil::Ops ops;
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name + "/columns", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name + "/checksums", -1));
		ops.push_back(new zkutil::Op::Remove(replica_path + "/parts/" + part->name, -1));
		zookeeper->multi(ops);

		/// Если кусок нужно удалить, надежнее удалить директорию после изменений в ZooKeeper.
		if (!entry.detach)
			data.replaceParts({part}, {}, true);
	}

	LOG_INFO(log, (entry.detach ? "Detached " : "Removed ") << removed_parts << " parts inside " << entry.new_part_name << ".");

	if (unreplicated_data)
	{
		Poco::ScopedLock<Poco::FastMutex> unreplicated_lock(unreplicated_mutex);

		removed_parts = 0;
		parts = unreplicated_data->getDataParts();
		for (const auto & part : parts)
		{
			if (!ActiveDataPartSet::contains(entry.new_part_name, part->name))
				continue;
			LOG_DEBUG(log, "Removing unreplicated part " << part->name);
			++removed_parts;

			if (entry.detach)
				unreplicated_data->renameAndDetachPart(part, "");
			else
				unreplicated_data->replaceParts({part}, {}, false);
		}
	}
}

bool StorageReplicatedMergeTree::executeAttachPart(const StorageReplicatedMergeTree::LogEntry & entry)
{
	String source_path = (entry.attach_unreplicated ? "unreplicated/" : "detached/") + entry.source_part_name;

	LOG_INFO(log, "Attaching part " << entry.source_part_name << " from " << source_path << " as " << entry.new_part_name);

	if (!Poco::File(data.getFullPath() + source_path).exists())
	{
		LOG_INFO(log, "No part at " << source_path << ". Will fetch it instead");
		return false;
	}

	LOG_DEBUG(log, "Checking data");
	MergeTreeData::MutableDataPartPtr part = data.loadPartAndFixMetadata(source_path);

	zkutil::Ops ops;
	checkPartAndAddToZooKeeper(part, ops, entry.new_part_name);

	if (entry.attach_unreplicated && unreplicated_data)
	{
		MergeTreeData::DataPartPtr unreplicated_part = unreplicated_data->getPartIfExists(entry.source_part_name);
		if (unreplicated_part)
			unreplicated_data->detachPartInPlace(unreplicated_part);
		else
			LOG_WARNING(log, "Unreplicated part " << entry.source_part_name << " is already detached");
	}

	zookeeper->multi(ops);

	/// NOTE: Не можем использовать renameTempPartAndAdd, потому что кусок не временный - если что-то пойдет не так, его не нужно удалять.
	part->renameTo(entry.new_part_name);
	part->name = entry.new_part_name;
	ActiveDataPartSet::parsePartName(part->name, *part);

	data.attachPart(part);

	LOG_INFO(log, "Finished attaching part " << entry.new_part_name);

	/// На месте удаленных кусков могут появиться новые, с другими данными.
	context.resetCaches();

	return true;
}

void StorageReplicatedMergeTree::queueUpdatingThread()
{
	while (!shutdown_called)
	{
		try
		{
			pullLogsToQueue(queue_updating_event);

			queue_updating_event->wait();
		}
		catch (zkutil::KeeperException & e)
		{
			if (e.code == ZINVALIDSTATE)
				restarting_event.set();

			tryLogCurrentException(__PRETTY_FUNCTION__);

			queue_updating_event->tryWait(ERROR_SLEEP_MS);
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);

			queue_updating_event->tryWait(ERROR_SLEEP_MS);
		}
	}

	LOG_DEBUG(log, "queue updating thread finished");
}

bool StorageReplicatedMergeTree::queueTask(BackgroundProcessingPool::Context & pool_context)
{
	LogEntryPtr entry;

	try
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		bool empty = queue.empty();
		if (!empty)
		{
			for (LogEntries::iterator it = queue.begin(); it != queue.end(); ++it)
			{
				if (!(*it)->currently_executing && shouldExecuteLogEntry(**it))
				{
					entry = *it;
					entry->tagPartAsFuture(*this);
					queue.splice(queue.end(), queue, it);
					entry->currently_executing = true;
					break;
				}
			}
		}
	}
	catch (...)
	{
		tryLogCurrentException(__PRETTY_FUNCTION__);
	}

	if (!entry)
		return false;

	bool exception = true;
	bool success = false;

	try
	{
		if (executeLogEntry(*entry, pool_context))
		{
			auto code = zookeeper->tryRemove(replica_path + "/queue/" + entry->znode_name);

			if (code != ZOK)
				LOG_ERROR(log, "Couldn't remove " << replica_path + "/queue/" + entry->znode_name << ": "
					<< zkutil::ZooKeeper::error2string(code) + ". This shouldn't happen often.");

			success = true;
		}

		exception = false;
	}
	catch (Exception & e)
	{
		if (e.code() == ErrorCodes::NO_REPLICA_HAS_PART)
			/// Если ни у кого нет нужного куска, наверно, просто не все реплики работают; не будем писать в лог с уровнем Error.
			LOG_INFO(log, e.displayText());
		else
			tryLogCurrentException(__PRETTY_FUNCTION__);
	}
	catch (...)
	{
		tryLogCurrentException(__PRETTY_FUNCTION__);
	}

	entry->future_part_tagger = nullptr;

	std::unique_lock<std::mutex> lock(queue_mutex);

	entry->currently_executing = false;
	entry->execution_complete.notify_all();

	if (success)
	{
		/// Удалим задание из очереди.
		/// Нельзя просто обратиться по заранее сохраненному итератору, потому что задание мог успеть удалить кто-то другой.
		for (LogEntries::iterator it = queue.end(); it != queue.begin();)
		{
			--it;
			if (*it == entry)
			{
				queue.erase(it);
				break;
			}
		}
	}

	/// Если не было исключения, не нужно спать.
	return !exception;
}

void StorageReplicatedMergeTree::mergeSelectingThread()
{
	bool need_pull = true;

	/** Может много времени тратиться на определение, можно ли мерджить два рядом стоящих куска.
	  * Два рядом стоящих куска можно мерджить, если все номера блоков между их номерами не используются ("заброшены", abandoned).
	  * Это значит, что между этими кусками не может быть вставлен другой кусок.
	  *
	  * Но если номера соседних блоков отличаются достаточно сильно (обычно, если между ними много "заброшенных" блоков),
	  *  то делается слишком много чтений из ZooKeeper, чтобы узнать, можно ли их мерджить.
	  *
	  * Воспользуемся утверждением, что если пару кусков было можно мерджить, и их мердж ещё не запланирован,
	  *  то и сейчас их можно мерджить, и будем запоминать это состояние, чтобы не делать много раз одинаковые запросы в ZooKeeper.
	  *
	  * TODO Интересно, как это сочетается с DROP PARTITION и затем ATTACH PARTITION.
	  */
	std::set<std::pair<std::string, std::string>> memoized_parts_that_could_be_merged;

	auto can_merge = [&memoized_parts_that_could_be_merged, this]
		(const MergeTreeData::DataPartPtr & left, const MergeTreeData::DataPartPtr & right) -> bool
	{
		/// Если какой-то из кусков уже собираются слить в больший, не соглашаемся его сливать.
		if (virtual_parts.getContainingPart(left->name) != left->name ||
			virtual_parts.getContainingPart(right->name) != right->name)
			return false;

		auto key = std::make_pair(left->name, right->name);
		if (memoized_parts_that_could_be_merged.count(key))
			return true;

		String month_name = left->name.substr(0, 6);

		/// Можно слить куски, если все номера между ними заброшены - не соответствуют никаким блокам.
		for (UInt64 number = left->right + 1; number <= right->left - 1; ++number)	/// Номера блоков больше нуля.
		{
			String path1 = zookeeper_path +              "/block_numbers/" + month_name + "/block-" + padIndex(number);
			String path2 = zookeeper_path + "/nonincrement_block_numbers/" + month_name + "/block-" + padIndex(number);

			if (AbandonableLockInZooKeeper::check(path1, *zookeeper) != AbandonableLockInZooKeeper::ABANDONED &&
				AbandonableLockInZooKeeper::check(path2, *zookeeper) != AbandonableLockInZooKeeper::ABANDONED)
				return false;
		}

		memoized_parts_that_could_be_merged.insert(key);
		return true;
	};

	while (!shutdown_called && is_leader_node)
	{
		bool success = false;

		try
		{
			std::unique_lock<std::mutex> merge_selecting_lock(merge_selecting_mutex);

			if (need_pull)
			{
				/// Нужно загрузить новую запись в очередь перед тем, как выбирать куски для слияния.
				///  (чтобы кусок добавился в virtual_parts).
				pullLogsToQueue();
				need_pull = false;
			}

			size_t merges_queued = 0;
			/// Есть ли в очереди или в фоновом потоке мердж крупных кусков.
			bool has_big_merge = context.getBackgroundPool().getCounter("replicated big merges") > 0;

			if (!has_big_merge)
			{
				std::unique_lock<std::mutex> lock(queue_mutex);

				for (const auto & entry : queue)
				{
					if (entry->type == LogEntry::MERGE_PARTS)
					{
						++merges_queued;

						if (!has_big_merge)
						{
							for (const String & name : entry->parts_to_merge)
							{
								MergeTreeData::DataPartPtr part = data.getActiveContainingPart(name);
								if (!part || part->name != name)
									continue;
								if (part->size_in_bytes > data.settings.max_bytes_to_merge_parts_small)
								{
									has_big_merge = true;
									break;
								}
							}
						}
					}
				}
			}

			do
			{
				if (merges_queued >= data.settings.max_replicated_merges_in_queue)
					break;

				MergeTreeData::DataPartsVector parts;

				String merged_name;

				if (!merger.selectPartsToMerge(parts, merged_name, MergeTreeDataMerger::NO_LIMIT,
												false, false, has_big_merge, can_merge) &&
					!merger.selectPartsToMerge(parts, merged_name, MergeTreeDataMerger::NO_LIMIT,
												true, false, has_big_merge, can_merge))
					break;

				bool all_in_zk = true;
				for (const auto & part : parts)
				{
					/// Если о каком-то из кусков нет информации в ZK, не будем сливать.
					if (!zookeeper->exists(replica_path + "/parts/" + part->name))
					{
						LOG_WARNING(log, "Part " << part->name << " exists locally but not in ZooKeeper.");
						enqueuePartForCheck(part->name);
						all_in_zk = false;
					}
				}
				if (!all_in_zk)
					break;

				LogEntry entry;
				entry.type = LogEntry::MERGE_PARTS;
				entry.source_replica = replica_name;
				entry.new_part_name = merged_name;

				for (const auto & part : parts)
					entry.parts_to_merge.push_back(part->name);

				need_pull = true;

				zookeeper->create(zookeeper_path + "/log/log-", entry.toString(), zkutil::CreateMode::PersistentSequential);

				String month_name = parts[0]->name.substr(0, 6);
				for (size_t i = 0; i + 1 < parts.size(); ++i)
				{
					/// Уберем больше не нужные отметки о несуществующих блоках.
					for (UInt64 number = parts[i]->right + 1; number <= parts[i + 1]->left - 1; ++number)
					{
						zookeeper->tryRemove(zookeeper_path +              "/block_numbers/" + month_name + "/block-" + padIndex(number));
						zookeeper->tryRemove(zookeeper_path + "/nonincrement_block_numbers/" + month_name + "/block-" + padIndex(number));
					}
				}

				success = true;
			}
			while (false);
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);
		}

		if (shutdown_called || !is_leader_node)
			break;

		if (!success)
			merge_selecting_event.tryWait(MERGE_SELECTING_SLEEP_MS);
	}

	LOG_DEBUG(log, "Merge selecting thread finished");
}

void StorageReplicatedMergeTree::cleanupThread()
{
	while (!shutdown_called)
	{
		try
		{
			clearOldParts();

			if (unreplicated_data)
				unreplicated_data->clearOldParts();

			if (is_leader_node)
			{
				clearOldLogs();
				clearOldBlocks();
			}
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);
		}

		shutdown_event.tryWait(CLEANUP_SLEEP_MS);
	}

	LOG_DEBUG(log, "cleanup thread finished");
}

void StorageReplicatedMergeTree::alterThread()
{
	bool force_recheck_parts = true;

	while (!shutdown_called)
	{
		try
		{
			zkutil::Stat stat;
			String columns_str = zookeeper->get(zookeeper_path + "/columns", &stat, alter_thread_event);
			NamesAndTypesList columns = NamesAndTypesList::parse(columns_str, context.getDataTypeFactory());

			bool changed = false;

			/// Проверим, что описание столбцов изменилось.
			/// Чтобы не останавливать лишний раз все запросы в таблицу, проверим сначала под локом на чтение.
			{
				auto table_lock = lockStructure(false);
				if (columns != data.getColumnsList())
					changed = true;
			}

			MergeTreeData::DataParts parts;

			/// Если описание столбцов изменилось, обновим структуру таблицы локально.
			if (changed)
			{
				auto table_lock = lockStructureForAlter();
				if (columns != data.getColumnsList())
				{
					LOG_INFO(log, "Columns list changed in ZooKeeper. Applying changes locally.");
					InterpreterAlterQuery::updateMetadata(database_name, table_name, columns, context);
					data.setColumnsList(columns);
					if (unreplicated_data)
						unreplicated_data->setColumnsList(columns);
					columns_version = stat.version;
					LOG_INFO(log, "Applied changes to table.");

					/// Нужно получить список кусков под блокировкой таблицы, чтобы избежать race condition с мерджем.
					parts = data.getDataParts();
				}
				else
				{
					changed = false;
					columns_version = stat.version;
				}
			}

			/// Обновим куски.
			if (changed || force_recheck_parts)
			{
				if (changed)
					LOG_INFO(log, "ALTER-ing parts");

				int changed_parts = 0;

				if (!changed)
					parts = data.getDataParts();

				auto table_lock = lockStructure(false);

				for (const MergeTreeData::DataPartPtr & part : parts)
				{
					/// Обновим кусок и запишем результат во временные файлы.
					/// TODO: Можно пропускать проверку на слишком большие изменения, если в ZooKeeper есть, например,
					///  нода /flags/force_alter.
					auto transaction = data.alterDataPart(part, columns);

					if (!transaction)
						continue;

					++changed_parts;

					/// Обновим метаданные куска в ZooKeeper.
					zkutil::Ops ops;
					ops.push_back(new zkutil::Op::SetData(replica_path + "/parts/" + part->name + "/columns", part->columns.toString(), -1));
					ops.push_back(new zkutil::Op::SetData(replica_path + "/parts/" + part->name + "/checksums", part->checksums.toString(), -1));
					zookeeper->multi(ops);

					/// Применим изменения файлов.
					transaction->commit();
				}

				/// То же самое для нереплицируемых данных.
				if (unreplicated_data)
				{
					parts = unreplicated_data->getDataParts();

					for (const MergeTreeData::DataPartPtr & part : parts)
					{
						auto transaction = unreplicated_data->alterDataPart(part, columns);

						if (!transaction)
							continue;

						++changed_parts;

						transaction->commit();
					}
				}

				zookeeper->set(replica_path + "/columns", columns.toString());

				if (changed || changed_parts != 0)
					LOG_INFO(log, "ALTER-ed " << changed_parts << " parts");
				force_recheck_parts = false;
			}

			alter_thread_event->wait();
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);

			force_recheck_parts = true;

			alter_thread_event->tryWait(ERROR_SLEEP_MS);
		}
	}

	LOG_DEBUG(log, "alter thread finished");
}

void StorageReplicatedMergeTree::removePartAndEnqueueFetch(const String & part_name)
{
	String part_path = replica_path + "/parts/" + part_name;

	LogEntryPtr log_entry = new LogEntry;
	log_entry->type = LogEntry::GET_PART;
	log_entry->source_replica = "";
	log_entry->new_part_name = part_name;

	zkutil::Ops ops;
	ops.push_back(new zkutil::Op::Create(
		replica_path + "/queue/queue-", log_entry->toString(), zookeeper->getDefaultACL(),
		zkutil::CreateMode::PersistentSequential));
	ops.push_back(new zkutil::Op::Remove(part_path + "/checksums", -1));
	ops.push_back(new zkutil::Op::Remove(part_path + "/columns", -1));
	ops.push_back(new zkutil::Op::Remove(part_path, -1));
	auto results = zookeeper->multi(ops);

	{
		std::unique_lock<std::mutex> lock(queue_mutex);

		String path_created = dynamic_cast<zkutil::Op::Create &>(ops[0]).getPathCreated();
		log_entry->znode_name = path_created.substr(path_created.find_last_of('/') + 1);
		log_entry->addResultToVirtualParts(*this);
		queue.push_back(log_entry);
	}
}

void StorageReplicatedMergeTree::enqueuePartForCheck(const String & name)
{
	Poco::ScopedLock<Poco::FastMutex> lock(parts_to_check_mutex);

	if (parts_to_check_set.count(name))
		return;
	parts_to_check_queue.push_back(name);
	parts_to_check_set.insert(name);
	parts_to_check_event.set();
}

void StorageReplicatedMergeTree::partCheckThread()
{
	while (!shutdown_called)
	{
		try
		{
			/// Достанем из очереди кусок для проверки.
			String part_name;
			{
				Poco::ScopedLock<Poco::FastMutex> lock(parts_to_check_mutex);
				if (parts_to_check_queue.empty())
				{
					if (!parts_to_check_set.empty())
					{
						LOG_ERROR(log, "Non-empty parts_to_check_set with empty parts_to_check_queue. This is a bug.");
						parts_to_check_set.clear();
					}
				}
				else
				{
					part_name = parts_to_check_queue.front();
				}
			}
			if (part_name.empty())
			{
				parts_to_check_event.wait();
				continue;
			}

			LOG_WARNING(log, "Checking part " << part_name);
			ProfileEvents::increment(ProfileEvents::ReplicatedPartChecks);

			auto part = data.getActiveContainingPart(part_name);
			String part_path = replica_path + "/parts/" + part_name;

			/// Этого или покрывающего куска у нас нет.
			if (!part)
			{
				/// Если кусок есть в ZooKeeper, удалим его оттуда и добавим в очередь задание скачать его.
				if (zookeeper->exists(part_path))
				{
					LOG_WARNING(log, "Part " << part_name << " exists in ZooKeeper but not locally. "
						"Removing from ZooKeeper and queueing a fetch.");
					ProfileEvents::increment(ProfileEvents::ReplicatedPartChecksFailed);

					removePartAndEnqueueFetch(part_name);
				}
				/// Если куска нет в ZooKeeper, проверим есть ли он хоть у кого-то.
				else
				{
					ActiveDataPartSet::Part part_info;
					ActiveDataPartSet::parsePartName(part_name, part_info);

					/** Будем проверять только куски, не полученные в результате слияния.
					  * Для кусков, полученных в результате слияния, такая проверка была бы некорректной,
					  *  потому что слитого куска может еще ни у кого не быть.
					  */
					if (part_info.left == part_info.right)
					{
						LOG_WARNING(log, "Checking if anyone has part covering " << part_name << ".");

						bool found = false;
						Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");
						for (const String & replica : replicas)
						{
							Strings parts = zookeeper->getChildren(zookeeper_path + "/replicas/" + replica + "/parts");
							for (const String & part_on_replica : parts)
							{
								if (part_on_replica == part_name || ActiveDataPartSet::contains(part_on_replica, part_name))
								{
									found = true;
									LOG_WARNING(log, "Found part " << part_on_replica << " on " << replica);
									break;
								}
							}
							if (found)
								break;
						}

						if (!found)
						{
							LOG_ERROR(log, "No replica has part covering " << part_name);
							ProfileEvents::increment(ProfileEvents::ReplicatedPartChecksFailed);

							/// Если ни у кого нет такого куска, удалим его из нашей очереди.

							bool was_in_queue = false;

							{
								std::unique_lock<std::mutex> lock(queue_mutex);

								for (LogEntries::iterator it = queue.begin(); it != queue.end(); )
								{
									if ((*it)->new_part_name == part_name)
									{
										zookeeper->tryRemove(replica_path + "/queue/" + (*it)->znode_name);
										queue.erase(it++);
										was_in_queue = true;
									}
									else
									{
										++it;
									}
								}
							}

							if (was_in_queue)
							{
								/** Такая ситуация возможна, если на всех репликах, где был кусок, он испортился.
								  * Например, у реплики, которая только что его записала, отключили питание, и данные не записались из кеша на диск.
								  */
								LOG_ERROR(log, "Part " << part_name << " is lost forever. Say goodbye to a piece of data!");

								/** Нужно добавить отсутствующий кусок в block_numbers, чтобы он не мешал слияниям.
								  * Вот только в сам block_numbers мы его добавить не можем - если так сделать,
								  *  ZooKeeper зачем-то пропустит один номер для автоинкремента,
								  *  и в номерах блоков все равно останется дырка.
								  * Специально из-за этого приходится отдельно иметь nonincrement_block_numbers.
								  */
								zookeeper->createIfNotExists(zookeeper_path + "/nonincrement_block_numbers", "");
								zookeeper->createIfNotExists(zookeeper_path + "/nonincrement_block_numbers/" + part_name.substr(0, 6), "");
								AbandonableLockInZooKeeper::createAbandonedIfNotExists(
									zookeeper_path + "/nonincrement_block_numbers/" + part_name.substr(0, 6) + "/block-" + padIndex(part_info.left),
									*zookeeper);
							}
						}
					}
				}
			}
			/// У нас есть этот кусок, и он активен.
			else if (part->name == part_name)
			{
				auto table_lock = lockStructure(false);

				/// Если кусок есть в ZooKeeper, сверим его данные с его чексуммами, а их с ZooKeeper.
				if (zookeeper->exists(replica_path + "/parts/" + part_name))
				{
					LOG_WARNING(log, "Checking data of part " << part_name << ".");

					try
					{
						auto zk_checksums = MergeTreeData::DataPart::Checksums::parse(
							zookeeper->get(replica_path + "/parts/" + part_name + "/checksums"));
						zk_checksums.checkEqual(part->checksums, true);

						auto zk_columns = NamesAndTypesList::parse(
							zookeeper->get(replica_path + "/parts/" + part_name + "/columns"), context.getDataTypeFactory());
						if (part->columns != zk_columns)
							throw Exception("Columns of local part " + part_name + " are different from ZooKeeper");

						MergeTreePartChecker::Settings settings;
						settings.setIndexGranularity(data.index_granularity);
						settings.setRequireChecksums(true);
						settings.setRequireColumnFiles(true);
						MergeTreePartChecker::checkDataPart(
							data.getFullPath() + part_name, settings, context.getDataTypeFactory());

						LOG_INFO(log, "Part " << part_name << " looks good.");
					}
					catch (...)
					{
						tryLogCurrentException(__PRETTY_FUNCTION__);

						LOG_ERROR(log, "Part " << part_name << " looks broken. Removing it and queueing a fetch.");
						ProfileEvents::increment(ProfileEvents::ReplicatedPartChecksFailed);

						removePartAndEnqueueFetch(part_name);

						/// Удалим кусок локально.
						data.renameAndDetachPart(part, "broken_");
					}
				}
				/// Если куска нет в ZooKeeper, удалим его локально.
				/// Возможно, кусок кто-то только что записал, и еще не успел добавить в ZK.
				/// Поэтому удаляем только если кусок старый (не очень надежно).
				else if (part->modification_time + 5 * 60 < time(0))
				{
					ProfileEvents::increment(ProfileEvents::ReplicatedPartChecksFailed);

					LOG_ERROR(log, "Unexpected part " << part_name << ". Removing.");
					data.renameAndDetachPart(part, "unexpected_");
				}
			}
			else
			{
				/// Если у нас есть покрывающий кусок, игнорируем все проблемы с этим куском.
				/// В худшем случае в лог еще old_parts_lifetime секунд будут валиться ошибки, пока кусок не удалится как старый.
			}

			/// Удалим кусок из очереди.
			{
				Poco::ScopedLock<Poco::FastMutex> lock(parts_to_check_mutex);
				if (parts_to_check_queue.empty() || parts_to_check_queue.front() != part_name)
				{
					LOG_ERROR(log, "Someone changed parts_to_check_queue.front(). This is a bug.");
				}
				else
				{
					parts_to_check_queue.pop_front();
					parts_to_check_set.erase(part_name);
				}
			}
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);
			parts_to_check_event.tryWait(ERROR_SLEEP_MS);
		}
	}
}


void StorageReplicatedMergeTree::becomeLeader()
{
	LOG_INFO(log, "Became leader");
	is_leader_node = true;
	merge_selecting_thread = std::thread(&StorageReplicatedMergeTree::mergeSelectingThread, this);
}

String StorageReplicatedMergeTree::findReplicaHavingPart(const String & part_name, bool active)
{
	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");

	/// Из реплик, у которых есть кусок, выберем одну равновероятно.
	std::random_shuffle(replicas.begin(), replicas.end());

	for (const String & replica : replicas)
	{
		if (zookeeper->exists(zookeeper_path + "/replicas/" + replica + "/parts/" + part_name) &&
			(!active || zookeeper->exists(zookeeper_path + "/replicas/" + replica + "/is_active")))
			return replica;
	}

	return "";
}

void StorageReplicatedMergeTree::fetchPart(const String & part_name, const String & replica_name)
{
	LOG_DEBUG(log, "Fetching part " << part_name << " from " << replica_name);

	auto table_lock = lockStructure(true);

	String host;
	int port;

	String host_port_str = zookeeper->get(zookeeper_path + "/replicas/" + replica_name + "/host");
	ReadBufferFromString buf(host_port_str);
	assertString("host: ", buf);
	readString(host, buf);
	assertString("\nport: ", buf);
	readText(port, buf);
	assertString("\n", buf);
	assertEOF(buf);

	MergeTreeData::MutableDataPartPtr part = fetcher.fetchPart(part_name, zookeeper_path + "/replicas/" + replica_name, host, port);

	zkutil::Ops ops;
	checkPartAndAddToZooKeeper(part, ops, part_name);

	MergeTreeData::Transaction transaction;
	auto removed_parts = data.renameTempPartAndReplace(part, nullptr, &transaction);

	zookeeper->multi(ops);
	transaction.commit();
	merge_selecting_event.set();

	for (const auto & removed_part : removed_parts)
	{
		LOG_DEBUG(log, "Part " << removed_part->name << " is rendered obsolete by fetching part " << part_name);
		ProfileEvents::increment(ProfileEvents::ObsoleteReplicatedParts);
	}

	ProfileEvents::increment(ProfileEvents::ReplicatedPartFetches);

	LOG_DEBUG(log, "Fetched part " << part_name << " from " << replica_name);
}

void StorageReplicatedMergeTree::shutdown()
{
	if (permanent_shutdown_called)
	{
		if (restarting_thread.joinable())
			restarting_thread.join();
		return;
	}

	permanent_shutdown_called = true;
	restarting_event.set();
	restarting_thread.join();

	endpoint_holder = nullptr;
}

void StorageReplicatedMergeTree::partialShutdown()
{
	leader_election = nullptr;
	shutdown_called = true;
	shutdown_event.set();
	merge_selecting_event.set();
	queue_updating_event->set();
	alter_thread_event->set();
	alter_query_event->set();
	parts_to_check_event.set();
	replica_is_active_node = nullptr;

	merger.cancelAll();
	if (unreplicated_merger)
		unreplicated_merger->cancelAll();

	LOG_TRACE(log, "Waiting for threads to finish");
	if (is_leader_node)
	{
		is_leader_node = false;
		if (merge_selecting_thread.joinable())
			merge_selecting_thread.join();
	}
	if (queue_updating_thread.joinable())
		queue_updating_thread.join();
	if (cleanup_thread.joinable())
		cleanup_thread.join();
	if (alter_thread.joinable())
		alter_thread.join();
	if (part_check_thread.joinable())
		part_check_thread.join();
	if (queue_task_handle)
		context.getBackgroundPool().removeTask(queue_task_handle);
	queue_task_handle.reset();
	LOG_TRACE(log, "Threads finished");
}

void StorageReplicatedMergeTree::goReadOnlyPermanently()
{
	LOG_INFO(log, "Going to read-only mode");

	is_read_only = true;
	permanent_shutdown_called = true;
	restarting_event.set();

	partialShutdown();
}

bool StorageReplicatedMergeTree::tryStartup()
{
	try
	{
		activateReplica();

		leader_election = new zkutil::LeaderElection(zookeeper_path + "/leader_election", *zookeeper,
			std::bind(&StorageReplicatedMergeTree::becomeLeader, this), replica_name);

		/// Все, что выше, может бросить KeeperException, если что-то не так с ZK.
		/// Все, что ниже, не должно бросать исключений.

		shutdown_called = false;
		shutdown_event.reset();

		merger.uncancelAll();
		if (unreplicated_merger)
			unreplicated_merger->uncancelAll();

		queue_updating_thread = std::thread(&StorageReplicatedMergeTree::queueUpdatingThread, this);
		cleanup_thread = std::thread(&StorageReplicatedMergeTree::cleanupThread, this);
		alter_thread = std::thread(&StorageReplicatedMergeTree::alterThread, this);
		part_check_thread = std::thread(&StorageReplicatedMergeTree::partCheckThread, this);
		queue_task_handle = context.getBackgroundPool().addTask(
			std::bind(&StorageReplicatedMergeTree::queueTask, this, std::placeholders::_1));
		queue_task_handle->wake();
		return true;
	}
	catch (const zkutil::KeeperException & e)
	{
		replica_is_active_node = nullptr;
		leader_election = nullptr;
		LOG_ERROR(log, "Couldn't start replication: " << e.what() << ", " << e.displayText() << ", stack trace:\n"
			<< e.getStackTrace().toString());
		return false;
	}
	catch (const Exception & e)
	{
		if (e.code() != ErrorCodes::REPLICA_IS_ALREADY_ACTIVE)
			throw;

		replica_is_active_node = nullptr;
		leader_election = nullptr;
		LOG_ERROR(log, "Couldn't start replication: " << e.what() << ", " << e.displayText() << ", stack trace:\n"
			<< e.getStackTrace().toString());
		return false;
	}
	catch (...)
	{
		replica_is_active_node = nullptr;
		leader_election = nullptr;
		throw;
	}
}

void StorageReplicatedMergeTree::restartingThread()
{
	try
	{
		/// Запуск реплики при старте сервера/создании таблицы.
		while (!permanent_shutdown_called && !tryStartup())
			restarting_event.tryWait(10 * 1000);

		/// Цикл перезапуска реплики при истечении сессии с ZK.
		while (!permanent_shutdown_called)
		{
			if (zookeeper->expired())
			{
				LOG_WARNING(log, "ZooKeeper session has expired. Switching to a new session.");

				partialShutdown();
				zookeeper = context.getZooKeeper();
				is_read_only = true;

				while (!permanent_shutdown_called && !tryStartup())
					restarting_event.tryWait(10 * 1000);

				if (permanent_shutdown_called)
					break;

				is_read_only = false;
			}

			restarting_event.tryWait(60 * 1000);
		}
	}
	catch (...)
	{
		tryLogCurrentException("StorageReplicatedMergeTree::restartingThread");
		LOG_ERROR(log, "Unexpected exception in restartingThread. The storage will be read-only until server restart.");
		goReadOnlyPermanently();
		LOG_DEBUG(log, "restarting thread finished");
		return;
	}

	try
	{
		endpoint_holder = nullptr;
		partialShutdown();
	}
	catch (...)
	{
		tryLogCurrentException("StorageReplicatedMergeTree::restartingThread");
	}

	LOG_DEBUG(log, "restarting thread finished");
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
	Names virt_column_names;
	Names real_column_names;
	for (const auto & it : column_names)
		if (it == "_replicated")
			virt_column_names.push_back(it);
		else
			real_column_names.push_back(it);

	Block virtual_columns_block;
	ColumnUInt8 * column = new ColumnUInt8(2);
	ColumnPtr column_ptr = column;
	column->getData()[0] = 0;
	column->getData()[1] = 1;
	virtual_columns_block.insert(ColumnWithNameAndType(column_ptr, new DataTypeUInt8, "_replicated"));

	/// Если запрошен хотя бы один виртуальный столбец, пробуем индексировать
	if (!virt_column_names.empty())
		VirtualColumnUtils::filterBlockWithQuery(query->clone(), virtual_columns_block, context);

	std::multiset<UInt8> values = VirtualColumnUtils::extractSingleValueFromBlock<UInt8>(virtual_columns_block, "_replicated");

	BlockInputStreams res;

	size_t part_index = 0;

	if (unreplicated_reader && values.count(0))
	{
		res = unreplicated_reader->read(
			real_column_names, query, settings, processed_stage, max_block_size, threads, &part_index);

		for (auto & virtual_column : virt_column_names)
		{
			if (virtual_column == "_replicated")
			{
				for (auto & stream : res)
					stream = new AddingConstColumnBlockInputStream<UInt8>(stream, new DataTypeUInt8, 0, "_replicated");
			}
		}
	}

	if (values.count(1))
	{
		auto res2 = reader.read(real_column_names, query, settings, processed_stage, max_block_size, threads, &part_index);

		for (auto & virtual_column : virt_column_names)
		{
			if (virtual_column == "_replicated")
			{
				for (auto & stream : res2)
					stream = new AddingConstColumnBlockInputStream<UInt8>(stream, new DataTypeUInt8, 1, "_replicated");
			}
		}

		res.insert(res.end(), res2.begin(), res2.end());
	}

	return res;
}

BlockOutputStreamPtr StorageReplicatedMergeTree::write(ASTPtr query)
{
	if (is_read_only)
		throw Exception("Table is in read only mode", ErrorCodes::TABLE_IS_READ_ONLY);

	String insert_id;
	if (query)
		if (ASTInsertQuery * insert = typeid_cast<ASTInsertQuery *>(&*query))
			insert_id = insert->insert_id;

	return new ReplicatedMergeTreeBlockOutputStream(*this, insert_id);
}

bool StorageReplicatedMergeTree::optimize()
{
	/// Померджим какие-нибудь куски из директории unreplicated.
	/// TODO: Мерджить реплицируемые куски тоже.

	if (!unreplicated_data)
		return false;

	Poco::ScopedLock<Poco::FastMutex> lock(unreplicated_mutex);

	unreplicated_data->clearOldParts();

	MergeTreeData::DataPartsVector parts;
	String merged_name;
	auto always_can_merge = [](const MergeTreeData::DataPartPtr &a, const MergeTreeData::DataPartPtr &b) { return true; };
	if (!unreplicated_merger->selectPartsToMerge(parts, merged_name, 0, true, true, false, always_can_merge))
		return false;

	unreplicated_merger->mergeParts(parts, merged_name);
	return true;
}

void StorageReplicatedMergeTree::alter(const AlterCommands & params,
	const String & database_name, const String & table_name, Context & context)
{
	LOG_DEBUG(log, "Doing ALTER");

	NamesAndTypesList new_columns;
	String new_columns_str;
	int new_columns_version;
	zkutil::Stat stat;

	{
		auto table_lock = lockStructureForAlter();

		if (is_read_only)
			throw Exception("Can't ALTER read-only table", ErrorCodes::TABLE_IS_READ_ONLY);

		data.checkAlter(params);

		new_columns = data.getColumnsList();
		params.apply(new_columns);

		new_columns_str = new_columns.toString();

		/// Делаем ALTER.
		zookeeper->set(zookeeper_path + "/columns", new_columns_str, -1, &stat);

		new_columns_version = stat.version;
	}

	LOG_DEBUG(log, "Updated columns in ZooKeeper. Waiting for replicas to apply changes.");

	/// Ждем, пока все реплики обновят данные.

	/// Подпишемся на изменения столбцов, чтобы перестать ждать, если кто-то еще сделает ALTER.
	if (!zookeeper->exists(zookeeper_path + "/columns", &stat, alter_query_event))
		throw Exception(zookeeper_path + "/columns doesn't exist", ErrorCodes::NOT_FOUND_NODE);
	if (stat.version != new_columns_version)
	{
		LOG_WARNING(log, zookeeper_path + "/columns changed before this ALTER finished; "
			"overlapping ALTER-s are fine but use caution with nontransitive changes");
		return;
	}

	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");
	for (const String & replica : replicas)
	{
		LOG_DEBUG(log, "Waiting for " << replica << " to apply changes");

		while (!shutdown_called)
		{
			String replica_columns_str;

			/// Реплику могли успеть удалить.
			if (!zookeeper->tryGet(zookeeper_path + "/replicas/" + replica + "/columns", replica_columns_str, &stat))
			{
				LOG_WARNING(log, replica << " was removed");
				break;
			}

			int replica_columns_version = stat.version;

			if (replica_columns_str == new_columns_str)
				break;

			if (!zookeeper->exists(zookeeper_path + "/columns", &stat))
				throw Exception(zookeeper_path + "/columns doesn't exist", ErrorCodes::NOT_FOUND_NODE);
			if (stat.version != new_columns_version)
			{
				LOG_WARNING(log, zookeeper_path + "/columns changed before ALTER finished; "
					"overlapping ALTER-s are fine but use caution with nontransitive changes");
				return;
			}

			if (!zookeeper->exists(zookeeper_path + "/replicas/" + replica + "/columns", &stat, alter_query_event))
			{
				LOG_WARNING(log, replica << " was removed");
				break;
			}

			if (stat.version != replica_columns_version)
				continue;

			alter_query_event->wait();
		}

		if (shutdown_called)
			break;
	}

	LOG_DEBUG(log, "ALTER finished");
}

static bool isValidMonthName(const String & s)
{
	if (s.size() != 6)
		return false;
	if (!std::all_of(s.begin(), s.end(), isdigit))
		return false;
	DayNum_t date = DateLUT::instance().toDayNum(OrderedIdentifier2Date(s + "01"));
	/// Не можем просто сравнить date с нулем, потому что 0 тоже валидный DayNum.
	return s == toString(Date2OrderedIdentifier(DateLUT::instance().fromDayNum(date)) / 100);
}

/// Название воображаемого куска, покрывающего все возможные куски в указанном месяце с номерами в указанном диапазоне.
static String getFakePartNameForDrop(const String & month_name, UInt64 left, UInt64 right)
{
	/// Диапазон дат - весь месяц.
	DateLUT & lut = DateLUT::instance();
	time_t start_time = OrderedIdentifier2Date(month_name + "01");
	DayNum_t left_date = lut.toDayNum(start_time);
	DayNum_t right_date = DayNum_t(static_cast<size_t>(left_date) + lut.daysInMonth(start_time) - 1);

	/// Уровень - right-left+1: кусок не мог образоваться в результате такого или большего количества слияний.
	return ActiveDataPartSet::getPartName(left_date, right_date, left, right, right - left + 1);
}

void StorageReplicatedMergeTree::dropPartition(const Field & field, bool detach)
{
	String month_name = field.getType() == Field::Types::UInt64 ? toString(field.get<UInt64>()) : field.safeGet<String>();

	if (!isValidMonthName(month_name))
		throw Exception("Invalid partition format: " + month_name + ". Partition should consist of 6 digits: YYYYMM",
						ErrorCodes::INVALID_PARTITION_NAME);

	/// TODO: Делать запрос в лидера по TCP.
	if (!is_leader_node)
		throw Exception("DROP PARTITION can only be done on leader replica.", ErrorCodes::NOT_LEADER);


	/** Пропустим один номер в block_numbers для удаляемого месяца, и будем удалять только куски до этого номера.
	  * Это запретит мерджи удаляемых кусков с новыми вставляемыми данными.
	  * Инвариант: в логе не появятся слияния удаляемых кусков с другими кусками.
	  * NOTE: Если понадобится аналогично поддержать запрос DROP PART, для него придется придумать какой-нибудь новый механизм,
	  *        чтобы гарантировать этот инвариант.
	  */
	UInt64 right;

	{
		AbandonableLockInZooKeeper block_number_lock = allocateBlockNumber(month_name);
		right = block_number_lock.getNumber();
		block_number_lock.unlock();
	}

	/// Такого никогда не должно происходить.
	if (right == 0)
		return;
	--right;

	String fake_part_name = getFakePartNameForDrop(month_name, 0, right);

	/** Запретим выбирать для слияния удаляемые куски - сделаем вид, что их всех уже собираются слить в fake_part_name.
	  * Инвариант: после появления в логе записи DROP_RANGE, в логе не появятся слияния удаляемых кусков.
	  */
	{
		std::unique_lock<std::mutex> merge_selecting_lock(merge_selecting_mutex);

		virtual_parts.add(fake_part_name);
	}

	/// Наконец, добившись нужных инвариантов, можно положить запись в лог.
	LogEntry entry;
	entry.type = LogEntry::DROP_RANGE;
	entry.source_replica = replica_name;
	entry.new_part_name = fake_part_name;
	entry.detach = detach;
	String log_znode_path = zookeeper->create(zookeeper_path + "/log/log-", entry.toString(), zkutil::CreateMode::PersistentSequential);
	entry.znode_name = log_znode_path.substr(log_znode_path.find_last_of('/') + 1);

	/// Дождемся, пока все реплики выполнят дроп.
	waitForAllReplicasToProcessLogEntry(entry);
}

void StorageReplicatedMergeTree::attachPartition(const Field & field, bool unreplicated, bool attach_part)
{
	String partition = field.getType() == Field::Types::UInt64 ? toString(field.get<UInt64>()) : field.safeGet<String>();

	if (!attach_part && !isValidMonthName(partition))
		throw Exception("Invalid partition format: " + partition + ". Partition should consist of 6 digits: YYYYMM",
						ErrorCodes::INVALID_PARTITION_NAME);

	String source_dir = (unreplicated ? "unreplicated/" : "detached/");

	/// Составим список кусков, которые нужно добавить.
	Strings parts;
	if (attach_part)
	{
		parts.push_back(partition);
	}
	else
	{
		LOG_DEBUG(log, "Looking for parts for partition " << partition << " in " << source_dir);
		ActiveDataPartSet active_parts;
		for (Poco::DirectoryIterator it = Poco::DirectoryIterator(full_path + source_dir); it != Poco::DirectoryIterator(); ++it)
		{
			String name = it.name();
			if (!ActiveDataPartSet::isPartDirectory(name))
				continue;
			if (name.substr(0, partition.size()) != partition)
				continue;
			LOG_DEBUG(log, "Found part " << name);
			active_parts.add(name);
		}
		LOG_DEBUG(log, active_parts.size() << " of them are active");
		parts = active_parts.getParts();
	}

	/// Синхронно проверим, что добавляемые куски существуют и не испорчены хотя бы на этой реплике. Запишем checksums.txt, если его нет.
	LOG_DEBUG(log, "Checking parts");
	for (const String & part : parts)
	{
		LOG_DEBUG(log, "Checking part " << part);
		data.loadPartAndFixMetadata(source_dir + part);
	}

	/// Выделим добавляемым кускам максимальные свободные номера, меньшие RESERVED_BLOCK_NUMBERS.
	/// NOTE: Проверка свободности номеров никак не синхронизируется. Выполнять несколько запросов ATTACH/DETACH/DROP одновременно нельзя.
	UInt64 min_used_number = RESERVED_BLOCK_NUMBERS;

	{
		auto existing_parts = data.getDataParts();
		for (const auto & part : existing_parts)
		{
			min_used_number = std::min(min_used_number, part->left);
		}
	}

	if (parts.size() > min_used_number)
		throw Exception("Not enough free small block numbers for attaching parts: "
			+ toString(parts.size()) + " needed, " + toString(min_used_number) + " available", ErrorCodes::NOT_ENOUGH_BLOCK_NUMBERS);

	/// Добавим записи в лог.
	std::reverse(parts.begin(), parts.end());
	std::list<LogEntry> entries;
	zkutil::Ops ops;
	for (const String & part_name : parts)
	{
		ActiveDataPartSet::Part part;
		ActiveDataPartSet::parsePartName(part_name, part);
		part.left = part.right = --min_used_number;
		String new_part_name = ActiveDataPartSet::getPartName(part.left_date, part.right_date, part.left, part.right, part.level);

		LOG_INFO(log, "Will attach " << part_name << " as " << new_part_name);

		entries.emplace_back();
		LogEntry & entry = entries.back();
		entry.type = LogEntry::ATTACH_PART;
		entry.source_replica = replica_name;
		entry.source_part_name = part_name;
		entry.new_part_name = new_part_name;
		entry.attach_unreplicated = unreplicated;
		ops.push_back(new zkutil::Op::Create(
			zookeeper_path + "/log/log-", entry.toString(), zookeeper->getDefaultACL(), zkutil::CreateMode::PersistentSequential));
	}

	LOG_DEBUG(log, "Adding attaches to log");
	zookeeper->multi(ops);
	size_t i = 0;
	for (LogEntry & entry : entries)
	{
		String log_znode_path = dynamic_cast<zkutil::Op::Create &>(ops[i++]).getPathCreated();
		entry.znode_name = log_znode_path.substr(log_znode_path.find_last_of('/') + 1);

		waitForAllReplicasToProcessLogEntry(entry);
	}
}

void StorageReplicatedMergeTree::drop()
{
	if (is_read_only)
		throw Exception("Can't drop read-only replicated table (need to drop data in ZooKeeper as well)", ErrorCodes::TABLE_IS_READ_ONLY);

	shutdown();

	LOG_INFO(log, "Removing replica " << replica_path);
	replica_is_active_node = nullptr;
	zookeeper->tryRemoveRecursive(replica_path);

	/// Проверяем, что zookeeper_path существует: его могла удалить другая реплика после выполнения предыдущей строки.
	Strings replicas;
	if (zookeeper->tryGetChildren(zookeeper_path + "/replicas", replicas) == ZOK && replicas.empty())
	{
		LOG_INFO(log, "Removing table " << zookeeper_path << " (this might take several minutes)");
		zookeeper->tryRemoveRecursive(zookeeper_path);
	}

	data.dropAllData();
}

void StorageReplicatedMergeTree::rename(const String & new_path_to_db, const String & new_database_name, const String & new_table_name)
{
	std::string new_full_path = new_path_to_db + escapeForFileName(new_table_name) + '/';

	data.setPath(new_full_path, true);
	if (unreplicated_data)
		unreplicated_data->setPath(new_full_path + "unreplicated/", false);

	database_name = new_database_name;
	table_name = new_table_name;
	full_path = new_full_path;

	/// TODO: Можно обновить названия логгеров.
}

AbandonableLockInZooKeeper StorageReplicatedMergeTree::allocateBlockNumber(const String & month_name)
{
	String month_path = zookeeper_path + "/block_numbers/" + month_name;
	if (!zookeeper->exists(month_path))
	{
		/// Создадим в block_numbers ноду для месяца и пропустим в ней 200 значений инкремента.
		/// Нужно, чтобы в будущем при необходимости можно было добавить данные в начало.
		zkutil::Ops ops;
		auto acl = zookeeper->getDefaultACL();
		ops.push_back(new zkutil::Op::Create(month_path, "", acl, zkutil::CreateMode::Persistent));
		for (size_t i = 0; i < RESERVED_BLOCK_NUMBERS; ++i)
		{
			ops.push_back(new zkutil::Op::Create(month_path + "/skip_increment", "", acl, zkutil::CreateMode::Persistent));
			ops.push_back(new zkutil::Op::Remove(month_path + "/skip_increment", -1));
		}
		/// Игнорируем ошибки - не получиться могло только если кто-то еще выполнил эту строчку раньше нас.
		zookeeper->tryMulti(ops);
	}

	return AbandonableLockInZooKeeper(
		zookeeper_path + "/block_numbers/" + month_name + "/block-",
		zookeeper_path + "/temp", *zookeeper);
}

void StorageReplicatedMergeTree::waitForAllReplicasToProcessLogEntry(const LogEntry & entry)
{
	LOG_DEBUG(log, "Waiting for all replicas to process " << entry.znode_name);

	UInt64 log_index = parse<UInt64>(entry.znode_name.substr(entry.znode_name.size() - 10));
	String log_entry_str = entry.toString();

	Strings replicas = zookeeper->getChildren(zookeeper_path + "/replicas");
	for (const String & replica : replicas)
	{
		LOG_DEBUG(log, "Waiting for " << replica << " to pull " << entry.znode_name << " to queue");

		/// Дождемся, пока запись попадет в очередь реплики.
		while (true)
		{
			zkutil::EventPtr event = new Poco::Event;

			String pointer = zookeeper->get(zookeeper_path + "/replicas/" + replica + "/log_pointer", nullptr, event);
			if (!pointer.empty() && parse<UInt64>(pointer) > log_index)
				break;

			event->wait();
		}

		LOG_DEBUG(log, "Looking for " << entry.znode_name << " in " << replica << " queue");

		/// Найдем запись в очереди реплики.
		Strings queue_entries = zookeeper->getChildren(zookeeper_path + "/replicas/" + replica + "/queue");
		String entry_to_wait_for;

		for (const String & entry_name : queue_entries)
		{
			String queue_entry_str;
			bool exists = zookeeper->tryGet(zookeeper_path + "/replicas/" + replica + "/queue/" + entry_name, queue_entry_str);
			if (exists && queue_entry_str == log_entry_str)
			{
				entry_to_wait_for = entry_name;
				break;
			}
		}

		/// Пока искали запись, ее уже выполнили и удалили.
		if (entry_to_wait_for.empty())
			continue;

		LOG_DEBUG(log, "Waiting for " << entry_to_wait_for << " to disappear from " << replica << " queue");

		/// Дождемся, пока запись исчезнет из очереди реплики.
		while (true)
		{
			zkutil::EventPtr event = new Poco::Event;

			String unused;
			/// get вместо exists, чтобы не утек watch, если ноды уже нет.
			if (!zookeeper->tryGet(zookeeper_path + "/replicas/" + replica + "/queue/" + entry_to_wait_for, unused, nullptr, event))
				break;

			event->wait();
		}
	}

	LOG_DEBUG(log, "Finished waiting for all replicas to process " << entry.znode_name);
}


void StorageReplicatedMergeTree::LogEntry::writeText(WriteBuffer & out) const
{
	writeString("format version: 1\n", out);
	writeString("source replica: ", out);
	writeString(source_replica, out);
	writeString("\n", out);
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
		case DROP_RANGE:
			if (detach)
				writeString("detach\n", out);
			else
				writeString("drop\n", out);
			writeString(new_part_name, out);
			break;
		case ATTACH_PART:
			writeString("attach\n", out);
			if (attach_unreplicated)
				writeString("unreplicated\n", out);
			else
				writeString("detached\n", out);
			writeString(source_part_name, out);
			writeString("\ninto\n", out);
			writeString(new_part_name, out);
			break;
	}
	writeString("\n", out);
}

void StorageReplicatedMergeTree::LogEntry::readText(ReadBuffer & in)
{
	String type_str;

	assertString("format version: 1\n", in);
	assertString("source replica: ", in);
	readString(source_replica, in);
	assertString("\n", in);
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
	else if (type_str == "drop" || type_str == "detach")
	{
		type = DROP_RANGE;
		detach = type_str == "detach";
		readString(new_part_name, in);
	}
	else if (type_str == "attach")
	{
		type = ATTACH_PART;
		String source_type;
		readString(source_type, in);
		if (source_type == "unreplicated")
			attach_unreplicated = true;
		else if (source_type == "detached")
			attach_unreplicated = false;
		else
			throw Exception("Bad format: expected 'unreplicated' or 'detached', found '" + source_type + "'", ErrorCodes::CANNOT_PARSE_TEXT);
		assertString("\n", in);
		readString(source_part_name, in);
		assertString("\ninto\n", in);
		readString(new_part_name, in);
	}
	assertString("\n", in);
}

}
