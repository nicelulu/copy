#pragma once

#include <DB/Interpreters/Aggregator.h>
#include <DB/Interpreters/Expression.h>
#include <DB/DataStreams/IProfilingBlockInputStream.h>


namespace DB
{

using Poco::SharedPtr;


/** Агрегирует поток блоков, используя заданные столбцы-ключи и агрегатные функции.
  * Столбцы с агрегатными функциями добавляет в конец блока.
  * Агрегатные функции не финализируются, то есть, не заменяются на своё значение, а содержат промежуточное состояние вычислений.
  * Это необходимо, чтобы можно было продолжить агрегацию (например, объединяя потоки частично агрегированных данных).
  */
class AggregatingBlockInputStream : public IProfilingBlockInputStream
{
public:
	AggregatingBlockInputStream(BlockInputStreamPtr input_, const ColumnNumbers & keys_, AggregateDescriptions & aggregates_)
		: input(input_), aggregator(new Aggregator(keys_, aggregates_)), has_been_read(false)
	{
		children.push_back(input);
	}

	/** keys берутся из GROUP BY части запроса
	  * Агрегатные функции ищутся везде в выражении.
	  * Столбцы, соответствующие keys и аргументам агрегатных функций, уже должны быть вычислены.
	  */
	AggregatingBlockInputStream(BlockInputStreamPtr input_, SharedPtr<Expression> expression);

	Block readImpl();

	String getName() const { return "AggregatingBlockInputStream"; }

	BlockInputStreamPtr clone() { return new AggregatingBlockInputStream(*this); }

private:
	AggregatingBlockInputStream(const AggregatingBlockInputStream & src)
		: input(src.input), aggregator(src.aggregator), has_been_read(src.has_been_read) {}
	
	BlockInputStreamPtr input;
	SharedPtr<Aggregator> aggregator;
	bool has_been_read;
};

}
