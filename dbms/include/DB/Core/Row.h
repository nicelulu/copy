#ifndef DBMS_CORE_ROW_H
#define DBMS_CORE_ROW_H

#include <vector>

#include <DB/Core/Field.h>


namespace DB
{

/** Тип данных для представления одной строки таблицы в оперативке.
  * Внимание! Предпочтительно вместо единичных строк хранить блоки столбцов. См. Block.h
  */

typedef std::vector<Field> Row;

}

#endif
