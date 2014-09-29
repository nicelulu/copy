#pragma once

#include <DB/Storages/IStorage.h>
#include <DB/Interpreters/Context.h>
#include <DB/Storages/ColumnDefault.h>


namespace DB
{


/** Позволяет создать новую таблицу, или создать объект уже существующей таблицы, или создать БД, или создать объект уже существующей БД
  */
class InterpreterCreateQuery
{
public:
	InterpreterCreateQuery(ASTPtr query_ptr_, Context & context_);

	/** В случае таблицы: добавляет созданную таблицу в контекст, а также возвращает её.
	  * В случае БД: добавляет созданную БД в контекст и возвращает NULL.
	  * assume_metadata_exists - не проверять наличие файла с метаданными и не создавать его
	  *  (для случая выполнения запроса из существующего файла с метаданными).
	  */
	StoragePtr execute(bool assume_metadata_exists = false);

	/// Список столбцов с типами в AST.
	static ASTPtr formatColumns(const NamesAndTypesList & columns);

private:
	/// AST в список столбцов с типами. Столбцы типа Nested развернуты в список настоящих столбцов.
	using ColumnsAndDefaults = std::pair<NamesAndTypesList, ColumnDefaults>;
	ColumnsAndDefaults parseColumns(ASTPtr expression_list, const DataTypeFactory & data_type_factory);

	DataTypePtr deduceType(const ASTPtr & expr, const NamesAndTypesList & columns) const;

	ASTPtr query_ptr;
	Context context;
};


}
