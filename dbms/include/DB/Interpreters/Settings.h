#pragma once

#include <Poco/Timespan.h>
#include <DB/Core/Defines.h>
#include <DB/Core/Field.h>


namespace DB
{

/** Настройки выполнения запроса.
  */
struct Settings
{
	/// Максимальный размер блока для чтения
	size_t max_block_size;
	/// Максимальное количество потоков выполнения запроса
	size_t max_threads;
	/// Максимальное количество соединений при распределённой обработке одного запроса (должно быть больше, чем max_threads).
	size_t max_distributed_connections;
	/// Какую часть запроса можно прочитать в оперативку для парсинга (оставшиеся данные для INSERT, если есть, считываются позже)
	size_t max_query_size;
	/// Выполнять разные стадии конвейера выполнения запроса параллельно
	bool asynchronous;
	/// Интервал в микросекундах для проверки, не запрошена ли остановка выполнения запроса, и отправки прогресса.
	size_t interactive_delay;
	Poco::Timespan connect_timeout;
	Poco::Timespan receive_timeout;
	Poco::Timespan send_timeout;
	/// Блокироваться в цикле ожидания запроса в сервере на указанное количество секунд.
	size_t poll_interval;
	/// Максимальное количество соединений с одним удалённым сервером в пуле.
	size_t distributed_connections_pool_size;

	Settings() :
		max_block_size(DEFAULT_BLOCK_SIZE),
		max_threads(DEFAULT_MAX_THREADS),
		max_distributed_connections(DEFAULT_MAX_DISTRIBUTED_CONNECTIONS),
		max_query_size(DEFAULT_MAX_QUERY_SIZE),
		asynchronous(true),
		interactive_delay(DEFAULT_INTERACTIVE_DELAY),
		connect_timeout(DBMS_DEFAULT_CONNECT_TIMEOUT_SEC, 0),
		receive_timeout(DBMS_DEFAULT_RECEIVE_TIMEOUT_SEC, 0),
		send_timeout(DBMS_DEFAULT_SEND_TIMEOUT_SEC, 0),
		poll_interval(DBMS_DEFAULT_POLL_INTERVAL),
		distributed_connections_pool_size(DBMS_DEFAULT_DISTRIBUTED_CONNECTIONS_POOL_SIZE)
	{
	}

	/// Установить настройку по имени.
	void set(const String & name, const Field & value)
	{
			 if (name == "max_block_size")		max_block_size 		= boost::get<UInt64>(value);
		else if (name == "max_threads")			max_threads 		= boost::get<UInt64>(value);
		else if (name == "max_query_size")		max_query_size 		= boost::get<UInt64>(value);
		else if (name == "asynchronous")		asynchronous 		= boost::get<UInt64>(value);
		else if (name == "interactive_delay") 	interactive_delay 	= boost::get<UInt64>(value);
		else if (name == "connect_timeout")		connect_timeout 	= Poco::Timespan(boost::get<UInt64>(value), 0);
		else if (name == "receive_timeout")		receive_timeout 	= Poco::Timespan(boost::get<UInt64>(value), 0);
		else if (name == "send_timeout")		send_timeout 		= Poco::Timespan(boost::get<UInt64>(value), 0);
		else if (name == "poll_interval")		poll_interval 		= boost::get<UInt64>(value);
		else if (name == "max_distributed_connections") max_distributed_connections = boost::get<UInt64>(value);
		else if (name == "distributed_connections_pool_size") distributed_connections_pool_size = boost::get<UInt64>(value);
		else
			throw Exception("Unknown setting " + name, ErrorCodes::UNKNOWN_SETTING);
	}
};


}
