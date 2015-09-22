#pragma once

#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>
#include <DB/Core/Types.h>

#include <mutex>
#include <condition_variable>


struct Stat;


namespace DB
{

class ReadBuffer;
class WriteBuffer;
class StorageReplicatedMergeTree;


/// Добавляет кусок в множество future_parts; в деструкторе убирает.
struct FuturePartTagger
{
	String part;
	StorageReplicatedMergeTree & storage;

	FuturePartTagger(const String & part_, StorageReplicatedMergeTree & storage_);
	~FuturePartTagger();
};

typedef Poco::SharedPtr<FuturePartTagger> FuturePartTaggerPtr;


/// Запись о том, что нужно сделать.
struct ReplicatedMergeTreeLogEntry
{
	typedef Poco::SharedPtr<ReplicatedMergeTreeLogEntry> Ptr;

	enum Type
	{
		GET_PART,    /// Получить кусок с другой реплики.
		MERGE_PARTS, /// Слить куски.
		DROP_RANGE,  /// Удалить куски в указанном месяце в указанном диапазоне номеров.
		ATTACH_PART, /// Перенести кусок из директории detached или unreplicated.
	};

	String znode_name;

	Type type;
	String source_replica; /// Пустая строка значит, что эта запись была добавлена сразу в очередь, а не скопирована из лога.

	/// Имя куска, получающегося в результате.
	/// Для DROP_RANGE имя несуществующего куска. Нужно удалить все куски, покрытые им.
	String new_part_name;

	Strings parts_to_merge;

	/// Для DROP_RANGE, true значит, что куски нужно не удалить, а перенести в директорию detached.
	bool detach = false;

	/// Для ATTACH_PART имя куска в директории detached или unreplicated.
	String source_part_name;
	/// Нужно переносить из директории unreplicated, а не detached.
	bool attach_unreplicated;

	FuturePartTaggerPtr future_part_tagger;

	/// Доступ под queue_mutex.
	bool currently_executing = false;	/// Выполняется ли действие сейчас.
	/// Эти несколько полей имеют лишь информационный характер (для просмотра пользователем с помощью системных таблиц).
	/// Доступ под queue_mutex.
	size_t num_tries = 0;				/// Количество попыток выполнить действие (с момента старта сервера; включая выполняющееся).
	ExceptionPtr exception;				/// Последний эксепшен, в случае безуспешной попытки выполнить действие.
	time_t last_attempt_time = 0;		/// Время начала последней попытки выполнить действие.
	String last_action;					/// Что делается сейчас или делалось в последний раз.
	String postpone_reason;				/// Причина, по которой действие было отложено, если оно отложено.

	std::condition_variable execution_complete; /// Пробуждается когда currently_executing становится false.

	/// Время создания или время копирования из общего лога в очередь конкретной реплики.
	time_t create_time = 0;

	/// Величина кворума (для GET_PART) - ненулевое значение при включенной кворумной записи.
	size_t quorum = 0;

	void addResultToVirtualParts(StorageReplicatedMergeTree & storage);
	void tagPartAsFuture(StorageReplicatedMergeTree & storage);

	void writeText(WriteBuffer & out) const;
	void readText(ReadBuffer & in);

	String toString() const;
	static Ptr parse(const String & s, const Stat & stat);
};


}
