#pragma once

#include "OLAPQueryParser.h"
#include <DB/Interpreters/Context.h>
#include <Poco/Util/AbstractConfiguration.h>
#include "OLAPAttributesMetadata.h"


namespace DB
{
namespace OLAP
{

/// Конвертирует распаршенный XML-запрос в формате OLAP-server в SQL-подобный запрос для clickhouse.
class QueryConverter
{
public:
	QueryConverter(Poco::Util::AbstractConfiguration & config);
	
	/// Получает из запроса в формате OLAP-server запрос и настройки для clickhouse.
	void OLAPServerQueryToClickhouse(const QueryParseResult & query, Context & inout_context, std::string & out_query);
private:
	/// Значение атрибута, подходящее для вывода в ответ и для группировки по нему.
	std::string convertAttributeFormatted(const std::string & attribute, unsigned parameter);
	/// Числовое значение атрибута, подходящее для подстановки в условия, агрегатные функции и ключи сортировки.
	std::string convertAttributeNumeric(const std::string & attribute, unsigned parameter);
	
	/// <aggregates><aggregate> => SELECT x
	std::string convertAggregateFunction(const std::string & attribute, unsigned parameter, const std::string & function);
	/// <where><condition><rhs> => SELECT ... where F(A, x)
	std::string convertConstant(const std::string & attribute, const std::string & value);
	/// <where><condition> => SELECT ... WHERE x
	std::string convertCondition(const std::string & attribute, unsigned parameter, const std::string & relation, const std::string & rhs);
	/// ASC или DESC
	std::string convertSortDirection(const std::string & direction);
	/// <dates> => SELECT ... WHERE x
	std::string convertDateRange(time_t date_first, time_t date_last);
	/// <counter_id> => SELECT ... WHERE x
	std::string convertCounterID(CounterID_t CounterID);
	
	std::string getTableName(CounterID_t CounterID);
	std::string getHavingSection();

	void fillFormattedAttributeMap();
	void fillNumericAttributeMap();
	void fillFormattingAggregatedAttributeMap();
	
	std::string table_for_single_counter;
	std::string table_for_all_counters;
	
	/// Форматная строка для convertAttributeNumeric. Есть для всех атрибутов.
	std::map<std::string, std::string> numeric_attribute_map;
	/// Форматная строка для получения выводимого значения из агрегированного числового значения.
	std::map<std::string, std::string> formatting_aggregated_attribute_map;
	/// Форматная строка для convertAttributeFormatted.
	std::map<std::string, std::string> formatted_attribute_map;
	/// Парсеры значений атрибутов.
	AttributeMetadatas attribute_metadatas;
};

}
}
