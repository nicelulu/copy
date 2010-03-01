#ifndef DBMS_STORAGES_ISTORAGE_H
#define DBMS_STORAGES_ISTORAGE_H

#include <Poco/SharedPtr.h>


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
private:
	/** Установить указатель на таблицу и кол-группу.
	  * - часть инициализации, которая выполняется при инициализации таблицы.
	  * (инициализация хранилища выполняется в два шага:
	  * 1 - конструктор,
	  * 2 - добавление к таблице (выполняется в конструкторе Table))
	  */
	virtual void addToTable(Table * table_, ColumnGroup * column_group_) = 0;
	
public:
	/** Прочитать данные, соответствующие точному значению ключа или префиксу.
	  * Возвращает объект, с помощью которого можно последовательно читать данные.
	  */
	virtual Poco::SharedPtr<ITablePartReader> read(const Row & key) = 0;

	/** Записать пачку данных в таблицу, обновляя существующие данные, если они есть.
	  * @param data - набор данных вида ключ (набор столбцов) -> значение (набор столбцов)
	  * @param mask - битовая маска - какие столбцы входят в кол-группу,
	  * которую хранит это хранилище
	  */
	virtual void merge(const AggregatedRowSet & data, const ColumnMask & mask) = 0;

	virtual ~IStorage() {}
};

}

#endif
