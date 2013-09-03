#pragma once

#include <list>
#include <Poco/SharedPtr.h>
#include <statdaemons/Stopwatch.h>
#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>
#include <DB/IO/WriteHelpers.h>


namespace DB
{

/** Список исполняющихся в данный момент запросов.
  */

class ProcessList
{
	friend class Entry;
public:
	/// Запрос и таймер его выполнения.
	struct Element
	{
		std::string query;
		Stopwatch watch;

		Element(const std::string & query_) : query(query_) {}
	};

	/// list, чтобы итераторы не инвалидировались. NOTE: можно заменить на cyclic buffer, но почти незачем.
	typedef std::list<Element> Containter;

private:
	Containter cont;
	size_t cur_size;		/// В C++03 std::list::size не O(1).
	size_t max_size;	/// Если 0 - не ограничено. Иначе, если пытаемся добавить больше - кидается исключение.
	mutable Poco::FastMutex mutex;

	/// Держит итератор на список, и удаляет элемент из списка в деструкторе.
	class Entry
	{
	private:
		ProcessList & parent;
		Containter::iterator it;
	public:
		Entry(ProcessList & parent_, Containter::iterator it_)
			: parent(parent_), it(it_) {}

		~Entry()
		{
			Poco::ScopedLock<Poco::FastMutex> lock(parent.mutex);
			parent.cont.erase(it);
			--parent.cur_size;
		}
	};

public:
	ProcessList(size_t max_size_ = 0) : cur_size(0), max_size(max_size_) {}

	typedef Poco::SharedPtr<Entry> EntryPtr;

	/// Зарегистрировать выполняющийся запрос. Возвращает refcounted объект, который удаляет запрос из списка при уничтожении.
	EntryPtr insert(const std::string & query_)
	{
		EntryPtr res;

		{
			Poco::ScopedLock<Poco::FastMutex> lock(mutex);

			if (max_size && cur_size >= max_size)
				throw Exception("Too much simultaneous queries. Maximum: " + toString(max_size), ErrorCodes::TOO_MUCH_SIMULTANEOUS_QUERIES);

			++cur_size;
			res = new Entry(*this, cont.insert(cont.end(), Element(query_)));
		}

		return res;
	}

	/// Количество одновременно выполняющихся запросов.
	size_t size() const
	{
		Poco::ScopedLock<Poco::FastMutex> lock(mutex);
		return cur_size;
	}

	/// Получить текущее состояние (копию) списка запросов.
	Containter get() const
	{
		Containter res;

		{
			Poco::ScopedLock<Poco::FastMutex> lock(mutex);
			res = cont;
		}

		return res;
	}

	void setMaxSize(size_t max_size_)
	{
		Poco::ScopedLock<Poco::FastMutex> lock(mutex);
		max_size = max_size_;
	}
};

}
