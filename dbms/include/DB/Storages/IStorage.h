#pragma once

#include <Yandex/logger_useful.h>

#include <DB/Core/Defines.h>
#include <DB/Core/Names.h>
#include <DB/Core/NamesAndTypes.h>
#include <DB/Core/Exception.h>
#include <DB/Core/QueryProcessingStage.h>
#include <DB/DataStreams/IBlockInputStream.h>
#include <DB/DataStreams/IBlockOutputStream.h>
#include <DB/Parsers/IAST.h>
#include <DB/Interpreters/Settings.h>
#include <DB/Storages/StoragePtr.h>
#include <Poco/File.h>


namespace DB
{


/** Хранилище. Отвечает за:
  * - хранение данных таблицы;
  * - определение, в каком файле (или не файле) хранятся данные;
  * - поиск данных и обновление данных;
  * - структура хранения данных (сжатие, etc.)
  * - конкуррентный доступ к данным (блокировки, etc.)
  */
class IStorage : private boost::noncopyable
{
public:
	IStorage() : drop_on_destroy(false) {}
	
	/// Основное имя типа таблицы (например, StorageWithoutKey).
	virtual std::string getName() const = 0;

	/// Имя самой таблицы (например, hits)
	virtual std::string getTableName() const = 0;

	/** Получить список имён и типов столбцов таблицы.
	  */
	virtual const NamesAndTypesList & getColumnsList() const = 0;

	const DataTypePtr getDataTypeByName(const String & column_name) const;

	/** То же самое, но в виде блока-образца.
	  */
	Block getSampleBlock() const;

	/** Возвращает true, если хранилище получает данные с удалённого сервера или серверов.
	  */
	virtual bool isRemote() const { return false; }

	/** Возвращает true, если хранилище поддерживает запросы с секцией SAMPLE.
	 */
	virtual bool supportsSampling() const { return false; }

	/** Читать набор столбцов из таблицы.
	  * Принимает список столбцов, которых нужно прочитать, а также описание запроса,
	  *  из которого может быть извлечена информация о том, каким способом извлекать данные
	  *  (индексы, блокировки и т. п.)
	  * Возвращает поток с помощью которого можно последовательно читать данные
	  *  или несколько потоков для параллельного чтения данных.
	  * Также в processed_stage записывается, до какой стадии запрос был обработан.
	  * (Обычно функция только читает столбцы из списка, но в других случаях,
	  *  например, запрос может быть частично обработан на удалённом сервере.)
	  *
	  * settings - настройки на один запрос.
	  * Обычно Storage не заботится об этих настройках, так как они применяются в интерпретаторе.
	  * Но, например, при распределённой обработке запроса, настройки передаются на удалённый сервер.
	  *
	  * threads - рекомендация, сколько потоков возвращать,
	  *  если хранилище может возвращать разное количество потоков.
	  */
	virtual BlockInputStreams read(
		const Names & column_names,
		ASTPtr query,
		const Settings & settings,
		QueryProcessingStage::Enum & processed_stage,
		size_t max_block_size = DEFAULT_BLOCK_SIZE,
		unsigned threads = 1)
	{
		throw Exception("Method read() is not supported by storage " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	/** Пишет данные в таблицу.
	  * Принимает описание запроса, в котором может содержаться информация о методе записи данных.
	  * Возвращает объект, с помощью которого можно последовательно писать данные.
	  */
	virtual BlockOutputStreamPtr write(
		ASTPtr query)
	{
		throw Exception("Method write() is not supported by storage " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	/** Удалить данные таблицы. После вызова этого метода, использование объекта некорректно (его можно лишь уничтожить).
	  */
	void drop()
	{
		drop_on_destroy = true;
	}
	
	/** Вызывается перед удалением директории с данными.
	  */
	virtual void dropImpl() {}

	/** Переименовать таблицу.
	  * Переименование имени в файле с метаданными, имени в списке таблиц в оперативке, осуществляется отдельно.
	  * В этой функции нужно переименовать директорию с данными, если она есть.
	  */
	virtual void rename(const String & new_path_to_db, const String & new_name)
	{
		throw Exception("Method rename() is not supported by storage " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	/** ALTER таблицы в виде изменения столбцов, не затрагивающий изменение Storage или его параметров.
	  * (ALTER, затрагивающий изменение движка, делается внешним кодом, путём копирования данных.)
	  */
	virtual void alter(NamesAndTypesListPtr columns)
	{
		throw Exception("Method alter() is not supported by storage " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	/** Выполнить какую-либо фоновую работу. Например, объединение кусков в таблице типа MergeTree.
	  * Возвращает - была ли выполнена какая-либо работа.
	  */
	virtual bool optimize()
	{
		throw Exception("Method optimize() is not supported by storage " + getName(), ErrorCodes::NOT_IMPLEMENTED);
	}

	virtual ~IStorage() {}

	/** Проверить, что все запрошенные имена есть в таблице и заданы корректно.
	  * (список имён не пустой и имена не повторяются)
	  */
	void check(const Names & column_names) const;

	/** Проверить, что блок с данными для записи содержит все столбцы таблицы с правильными типами,
	  *  и содержит только столбцы таблицы.
	  */
	void check(const Block & block) const;
	
	/** Возвращает владеющий указатель на себя.
	  */ 
	StoragePtr thisPtr()
	{
		if (!this_ptr.lock())
		{
			boost::shared_ptr<StoragePtr::Wrapper> p(new StoragePtr::Wrapper(this));
			this_ptr = p;
			return StoragePtr(this_ptr);
		}
		else
		{
			return StoragePtr(this_ptr);
		}
	}
	
	/** Удаляет директорию в деструкторе.
	  */
	struct DatabaseDropper
	{
		DatabaseDropper(const std::string & data_path_) : data_path(data_path_) {}
		
		~DatabaseDropper()
		{
			if (std::uncaught_exception())
			{
				try
				{
					LOG_ERROR(&Logger::get("DatabaseDropper"), "Didn't remove database data directory because of uncaught exception.");
				}
				catch(...)
				{
				}
			}
			else
			{
				Poco::File(data_path).remove(false);
			}
		}
		
		std::string data_path;
	};
	
	/** Сюда кладется указатель на базу данных, когда ее нужно удалить (чтобы она удалилась после всех ее таблиц).
	  */
	boost::shared_ptr<DatabaseDropper> database_to_drop;
	
	bool drop_on_destroy;
	
	/** Директория с данными. Будет удалена после удаления таблицы (после вызова dropImpl).
	  */
	std::string path_to_remove_on_drop;

private:
	boost::weak_ptr<StoragePtr::Wrapper> this_ptr;
};

}
