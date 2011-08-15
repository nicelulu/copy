#pragma once

#include <Poco/SharedPtr.h>

#include <DB/Core/Defines.h>
#include <DB/Core/Names.h>
#include <DB/Core/Exception.h>

#include <DB/DataStreams/IBlockInputStream.h>
#include <DB/DataStreams/IBlockOutputStream.h>

#include <DB/Parsers/IAST.h>


namespace DB
{

using Poco::SharedPtr;

/** Хранилище. Отвечает за:
  * - хранение данных таблицы;
  * - определение, в каком файле (или не файле) хранятся данные;
  * - поиск данных и обновление данных;
  * - структура хранения данных (сжатие, etc.)
  * - конкуррентный доступ к данным (блокировки, etc.)
  */
class IStorage
{
public:
	/// Основное имя типа таблицы (например, StorageWithoutKey).
	virtual std::string getName() const = 0;

	/** Читать набор столбцов из таблицы.
	  * Принимает список столбцов, которых нужно прочитать, а также описание запроса,
	  *  из которого может быть извлечена информация о том, каким способом извлекать данные
	  *  (индексы, блокировки и т. п.)
	  * Возвращает объект, с помощью которого можно последовательно читать данные.
	  */
	virtual SharedPtr<IBlockInputStream> read(
		const Names & column_names,
		ASTPtr query,
		size_t max_block_size = DEFAULT_BLOCK_SIZE)
	{
		throw Exception("Method read() is not supported by storage " + getName());
	}

	/** Пишет данные в таблицу.
	  * Принимает описание запроса, в котором может содержаться информация о методе записи данных.
	  * Возвращает объект, с помощью которого можно последовательно писать данные.
	  */
	virtual SharedPtr<IBlockOutputStream> write(
		ASTPtr query)
	{
		throw Exception("Method write() is not supported by storage " + getName());
	}

	virtual ~IStorage() {}
};

typedef SharedPtr<IStorage> StoragePtr;

}
