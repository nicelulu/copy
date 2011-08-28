#pragma once

#include <DB/Interpreters/Context.h>
#include <DB/DataStreams/IBlockInputStream.h>


namespace DB
{


/** Интерпретирует запрос SELECT. Возвращает поток блоков с результатами выполнения запроса.
  */
class InterpreterSelectQuery
{
public:
	InterpreterSelectQuery(ASTPtr query_ptr_, Context & context_, size_t max_block_size_ = DEFAULT_BLOCK_SIZE);

	BlockInputStreamPtr execute();

	DataTypes getReturnTypes();

private:
	StoragePtr getTable();
	
	/** Пометить часть дерева запроса некоторым part_id.
	  * - для того, чтобы потом можно было вычислить только часть выражения из запроса.
	  */
	void setPartID(ASTPtr ast, unsigned part_id);

	enum PartID
	{
		PART_OTHER = 0,
		PART_SELECT = 1,
		PART_WHERE = 2,
		PART_HAVING = 3,
	};


	ASTPtr query_ptr;
	Context context;
	size_t max_block_size;
};


}
