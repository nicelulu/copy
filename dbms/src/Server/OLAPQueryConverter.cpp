#include "OLAPQueryConverter.h"
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/WriteBufferFromString.h>
#include <Poco/NumberFormatter.h>


namespace DB
{
namespace OLAP
{

QueryConverter::QueryConverter(Poco::Util::AbstractConfiguration & config)
{
	table_for_single_counter = config.getString("olap_table_for_single_counter");
	table_for_all_counters = config.getString("olap_table_for_all_counters");
	
	fillFormattedAttributeMap();
	fillNumericAttributeMap();
	fillFormattingAggregatedAttributeMap();
}

void QueryConverter::OLAPServerQueryToClickhouse(const QueryParseResult & query, Context & inout_context, std::string & out_query)
{
	/// Проверим, умеем ли мы выполнять такой запрос.
	if (query.is_list_of_visits_query)
		throw Exception("List of visits queries not supported", ErrorCodes::UNSUPPORTED_PARAMETER);
	if (query.format != FORMAT_TAB)
		throw Exception("Only tab-separated output format is supported", ErrorCodes::UNSUPPORTED_PARAMETER);
	
	/// Учтем некоторые настройки (пока далеко не все).
	
	Settings new_settings = inout_context.getSettings();
	
	if (query.concurrency != 0)
		new_settings.max_threads = query.concurrency;
	
	inout_context.setSettings(new_settings);
	
	/// Составим запрос.
	out_query = "SELECT ";
	
	std::vector<std::string> selected_expressions;
	
	/// Что выбирать: ключи агрегации и агрегированные значения.
	for (size_t i = 0; i < query.key_attributes.size(); ++i)
	{
		const QueryParseResult::KeyAttribute & key = query.key_attributes[i];
		std::string s = convertAttributeFormatted(key.attribute, key.parameter);
		
		if (i > 0)
			out_query += ", ";
		out_query += s;
		selected_expressions.push_back(s);
	}
	
	for (size_t i = 0; i < query.aggregates.size(); ++i)
	{
		const QueryParseResult::Aggregate & aggregate = query.aggregates[i];
		std::string s = convertAggregateFunction(aggregate.attribute, aggregate.parameter, aggregate.function);
		
		if (i > 0)
			out_query += ", ";
		out_query += s;
		selected_expressions.push_back(s);
	}
	
	/// Из какой таблицы.
	out_query += " FROM " + getTableName(query.CounterID);
	
	/// Условия.
	out_query += " WHERE ";
	
	/// Диапазон дат.
	out_query += convertDateRange(query.date_first, query.date_last);
	
	/// Счетчик.
	if (query.CounterID != 0)
		out_query += " AND " + convertCounterID(query.CounterID);
	
	/// Произвольные условия.
	for (size_t i = 0; i < query.where_conditions.size(); ++i)
	{
		const QueryParseResult::WhereCondition & condition = query.where_conditions[i];
		out_query += " AND " + convertCondition(condition.attribute, condition.parameter, condition.relation, condition.rhs);
	}
	
	/// Группировка.
	if (!query.key_attributes.empty())
	{
		out_query += " GROUP BY ";
		for (size_t i = 0; i < query.key_attributes.size(); ++i)
		{
			if (i > 0)
				out_query += ", ";
			out_query += selected_expressions[i];
		}
	}
	
	/// Условие для групп.
	out_query += " " + getHavingSection();
	
	/// Сортировка.
	if (!query.sort_columns.empty())
	{
		out_query += " ORDER BY ";
		for (size_t i = 0; i < query.sort_columns.size(); ++i)
		{
			const QueryParseResult::SortColumn & column = query.sort_columns[i];
			
			if (i > 0)
				out_query += ", ";
			out_query += selected_expressions[column.index];
			out_query += " " + convertSortDirection(column.direction);
		}
	}
	
	/// Ограничение на количество выводимых строк.
	if (query.limit != 0)
		out_query += " LIMIT " + Poco::NumberFormatter::format(query.limit);
}

std::string QueryConverter::convertAttributeFormatted(const std::string & attribute, unsigned parameter)
{
	if (formatted_attribute_map.count(attribute))
		Poco::format(formatted_attribute_map[attribute], parameter);
	
	if (numeric_attribute_map.count(attribute))
	{
		std::string numeric = Poco::format(numeric_attribute_map[attribute], parameter);
		
		if (formatting_aggregated_attribute_map.count(attribute))
			return Poco::format(formatting_aggregated_attribute_map[attribute], std::string("(") + numeric + ")");
		else
			return numeric;
	}
	
	throw Exception("Unknown attribute: " + attribute, ErrorCodes::UNKNOWN_IDENTIFIER);
}

std::string QueryConverter::convertAttributeNumeric(const std::string & attribute, unsigned parameter)
{
	if (numeric_attribute_map.count(attribute))
		return Poco::format(numeric_attribute_map[attribute], parameter);
	
	throw Exception("Unknown attribute: " + attribute, ErrorCodes::UNKNOWN_IDENTIFIER);
}

/// <aggregates><aggregate> => SELECT x
std::string QueryConverter::convertAggregateFunction(const std::string & attribute, unsigned parameter, const std::string & function)
{
	return "";
}

/// <where><condition><rhs> => SELECT ... where F(A, x)
std::string QueryConverter::convertConstant(const std::string & attribute, const std::string & value)
{
	return "";
}

/// <where><condition> => SELECT ... WHERE x
std::string QueryConverter::convertCondition(const std::string & attribute, unsigned parameter, const std::string & relation, const std::string & rhs)
{
	return "";
}

/// ASC или DESC
std::string QueryConverter::convertSortDirection(const std::string & direction)
{
	if (direction == "descending")
		return "DESC";
	else
		return "ASC";
}

/// <dates> => SELECT ... WHERE x
std::string QueryConverter::convertDateRange(time_t date_first, time_t date_last)
{
	std::string first_str;
	std::string last_str;
	{
		WriteBufferFromString first_buf(first_str);
		WriteBufferFromString last_buf(last_str);
		writeDateText(Yandex::DateLUTSingleton::instance().toDayNum(date_first), first_buf);
		writeDateText(Yandex::DateLUTSingleton::instance().toDayNum(date_last), last_buf);
	}
	return "StartDate >= '" + first_str + "' AND StartDate <= '" + last_str + "'";
}

/// <counter_id> => SELECT ... WHERE x
std::string QueryConverter::convertCounterID(Yandex::CounterID_t CounterID)
{
	return "CounterID == " + Poco::NumberFormatter::format(CounterID);
}

std::string QueryConverter::getTableName(Yandex::CounterID_t CounterID)
{
	if (CounterID == 0)
		return table_for_all_counters;
	else
		return table_for_single_counter;
}

std::string QueryConverter::getHavingSection()
{
	return "HAVING sum(Sign) > 0";
}

void QueryConverter::fillNumericAttributeMap()
{
#define M(a, b) numeric_attribute_map[a] = b;
	M("Dummy",                "0")
	M("VisitStartDateTime",   "toUInt32(StartTime)")
	M("VisitStartDate",       "toUInt32(toDateTime(StartDate))")
	M("VisitStartWeek",       "toUInt32(toDateTime(toMonday(StartDate)))")
	M("VisitStartTime",       "toUInt32(toTime(StartTime))")
	
	M("VisitStartYear",       "toYear(StartDate)")
	M("VisitStartMonth",      "toMonth(StartDate)")
	M("VisitStartDayOfWeek",  "toDayOfWeek(StartDate)")
	M("VisitStartDayOfMonth", "toDayOfMonth(StartDate)")
	M("VisitStartHour",       "toHour(StartTime)")
	M("VisitStartMinute",     "toMinute(StartTime)")
	M("VisitStartSecond",     "toSecond(StartTime)")

	M("FirstVisitDateTime",   "toUInt32(FirstVisit)")
	M("FirstVisitDate",       "toUInt32(toDateTime(toDate(FirstVisit)))")
	M("FirstVisitWeek",       "toUInt32(toDateTime(toMonday(FirstVisit)))")
	M("FirstVisitTime",       "toUInt32(toTime(FirstVisit))")
	
	M("FirstVisitYear",       "toYear(FirstVisit)")
	M("FirstVisitMonth",      "toMonth(FirstVisit)")
	M("FirstVisitDayOfWeek",  "toDayOfWeek(FirstVisit)")
	M("FirstVisitDayOfMonth", "toDayOfMonth(FirstVisit)")
	M("FirstVisitHour",       "toHour(FirstVisit)")
	M("FirstVisitMinute",     "toMinute(FirstVisit)")
	M("FirstVisitSecond",     "toSecond(FirstVisit)")

	M("PredLastVisitDate",    "toUInt32(toDateTime(PredLastVisit))")
	M("PredLastVisitWeek",    "toUInt32(toDateTime(toMonday(PredLastVisit)))")
	M("PredLastVisitYear",    "toYear(PredLastVisit)")
	M("PredLastVisitMonth",   "toMonth(PredLastVisit)")
	M("PredLastVisitDayOfWeek","toDayOfWeek(PredLastVisit)")
	M("PredLastVisitDayOfMonth","toDayOfMonth(PredLastVisit)")

	M("ClientDateTime",       "toUInt32(ClientEventTime)")
	M("ClientTime",           "toUInt32(toTime(ClientEventTime))")
	M("ClientTimeHour",       "toHour(ClientEventTime)")
	M("ClientTimeMinute",     "toMinute(ClientEventTime)")
	M("ClientTimeSecond",     "toSecond(ClientEventTime)")
	
	M("EndURLHash",           "halfMD5(EndURL)")
	M("RefererHash",          "halfMD5(Referer)")
	M("SearchPhraseHash",     "halfMD5(SearchPhrase)")
	M("RefererDomainHash",    "halfMD5(domainWithoutWWW(Referer))")
	M("StartURLHash",         "halfMD5(StartURL)")
	M("StartURLDomainHash",   "halfMD5(domainWithoutWWW(StartURL))")
	M("RegionID",             "RegionID")
	M("RegionCity",           "regionToCity(RegionID)")
	M("RegionArea",           "regionToArea(RegionID)")
	M("RegionCountry",        "regionToCountry(RegionID)")
	M("TraficSourceID",       "TraficSourceID")
	M("IsNewUser",            "FirstVisit == StartTime")
	M("UserNewness",          "intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400)")
	M("UserNewnessInterval",  "roundToExp2(intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400))")
	M("UserReturnTime",       "toUInt32(toDate(StartTime))-toUInt32(PredLastVisit)")
	M("UserReturnTimeInterval","roundToExp2(toUInt32(toDate(StartTime))-toUInt32(PredLastVisit))")
	M("UserVisitsPeriod",     "(TotalVisits <= 1 ? toUInt16(0) : toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1))))")
	M("UserVisitsPeriodInterval","(TotalVisits <= 1 ? toUInt16(0) : roundToExp2(toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1)))))")
	M("VisitTime",            "Duration")
	M("VisitTimeInterval",    "roundDuration(Duration)")
	M("PageViews",            "PageViews")
	M("PageViewsInterval",    "roundToExp2(PageViews)")
	M("Bounce",               "PageViews <= 1")
	M("BouncePrecise",        "IsBounce")
	M("IsYandex",             "IsYandex")
	M("UserID",               "UserID")
	
	M("UserIDCreateDateTime", "(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000)")
	M("UserIDCreateDate",     "(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000)")
	
	M("UserIDAge",            "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : intDiv(toInt64(StartTime) - UserID % 10000000000, 86400))")
	M("UserIDAgeInterval",    "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : toInt64(roundToExp2(intDiv(toUInt64(StartTime) - UserID % 10000000000, 86400))))")
	M("TotalVisits",          "TotalVisits")
	M("TotalVisitsInterval",  "roundToExp2(TotalVisits)")
	M("Age",                  "Age")
	M("AgeInterval",          "roundAge(Age)")
	M("Sex",                  "Sex")
	M("Income",               "Income")
	M("AdvEngineID",          "AdvEngineID")
	
	M("DotNet",               "NetMajor * 256 + NetMinor")
	
	M("DotNetMajor",          "NetMajor")
	
	M("Flash",                "FlashMajor * 256 + FlashMinor")
	
	M("FlashExists",          "FlashMajor > 0")
	M("FlashMajor",           "FlashMajor")
	
	M("Silverlight",          "SilverlightVersion1 * 72057594037927936 + SilverlightVersion2 * 281474976710656 + SilverlightVersion3 * 65536 + SilverlightVersion4")
	
	M("SilverlightMajor",     "SilverlightVersion1")
	M("Hits",                 "Hits")
	M("HitsInterval",         "roundToExp2(Hits)")
	M("JavaEnable",           "JavaEnable")
	M("CookieEnable",         "CookieEnable")
	M("JavascriptEnable",     "JavascriptEnable")
	M("IsMobile",             "IsMobile")
	M("MobilePhoneID",        "MobilePhone")
	M("MobilePhoneModelHash", "halfMD5(MobilePhoneModel)")
	
	M("MobilePhoneModel",     "reinterpretAsUInt64(MobilePhoneModel)")
	M("BrowserLanguage",      "BrowserLanguage")
	M("BrowserCountry",       "BrowserCountry")
	M("TopLevelDomain",       "reinterpretAsUInt64(topLevelDomain(StartURL))")
	M("URLScheme",            "reinterpretAsUInt64(protocol(StartURL))")
	
	M("IPNetworkID",          "IPNetworkID")
	M("ClientTimeZone",       "ClientTimeZone")
	M("OSID",                 "OS")
	M("OSMostAncestor",       "osToRoot(OS)")
	
	M("ClientIP",             "ClientIP")
	M("Resolution",           "ResolutionWidth * 16777216 + ResolutionHeight * 256 + ResolutionDepth")
	M("ResolutionWidthHeight","ResolutionWidth * 65536 + ResolutionHeight")
	
	M("ResolutionWidth",      "ResolutionWidth")
	M("ResolutionHeight",     "ResolutionHeight")
	M("ResolutionWidthInterval","intDiv(ResolutionWidth, 100) * 100")
	M("ResolutionHeightInterval","intDiv(ResolutionHeight, 100) * 100")
	M("ResolutionColor",      "ResolutionDepth")
	
	M("WindowClientArea",     "WindowClientWidth * 65536 + WindowClientHeight")
	
	M("WindowClientAreaInterval","intDiv(WindowClientWidth, 100) * 6553600 + intDiv(WindowClientHeight, 100) * 100")
	M("WindowClientWidth",    "WindowClientWidth")
	M("WindowClientWidthInterval","intDiv(WindowClientWidth, 100) * 100")
	M("WindowClientHeight",   "WindowClientHeight")
	M("WindowClientHeightInterval","intDiv(WindowClientHeight, 100) * 100")
	M("SearchEngineID",       "SearchEngineID")
	M("SEMostAncestor",       "seToRoot(SearchEngineID)")
	M("CodeVersion",          "CodeVersion")
	
	M("UserAgent",            "UserAgent * 16777216 + UserAgentMajor * 65536 + UserAgentMinor")
	M("UserAgentVersion",     "UserAgentMajor * 65536 + UserAgentMinor")
	M("UserAgentMajor",       "UserAgent * 256 + UserAgentMajor")
	
	M("UserAgentID",          "UserAgent")
	M("ClickGoodEvent",       "ClickGoodEvent")
	M("ClickPriorityID",      "ClickPriorityID")
	M("ClickBannerID",        "ClickBannerID")
	M("ClickPhraseID",        "ClickPhraseID")
	M("ClickPageID",          "ClickPageID")
	M("ClickPlaceID",         "ClickPlaceID")
	M("ClickTypeID",          "ClickTypeID")
	M("ClickResourceID",      "ClickResourceID")
	M("ClickDomainID",        "ClickDomainID")
	M("ClickCost",            "ClickCost")
	M("ClickURLHash",         "halfMD5(ClickURL)")
	M("ClickOrderID",         "ClickOrderID")
	M("GoalReachesAny",       "")
	M("GoalReachesDepth",     "length(GoalsReached)")
	M("GoalReachesURL",       "")
	M("ConvertedAny",         "")
	M("ConvertedDepth",       "")
	M("ConvertedURL",         "")
	M("GoalReaches",          "arrayCount(GoalsReached, %u)")
	M("Converted",            "has(GoalsReached, %u)")
	M("CounterID",            "CounterID")
	M("VisitID",              "VisitID")
	
	M("Interests",            "Interests")
	
	M("HasInterestPhoto",     "modulo(intDiv(Interests, 128), 2)")
	M("HasInterestMoviePremieres","modulo(intDiv(Interests, 64), 2)")
	M("HasInterestTourism",   "modulo(intDiv(Interests, 32), 2)")
	M("HasInterestFamilyAndChildren","modulo(intDiv(Interests, 16), 2)")
	M("HasInterestFinance",   "modulo(intDiv(Interests, 8), 2)")
	M("HasInterestB2B",       "modulo(intDiv(Interests, 4), 2)")
	M("HasInterestCars",      "modulo(intDiv(Interests, 2), 2)")
	M("HasInterestMobileAndInternetCommunications","modulo(Interests, 2)")
	M("HasInterestBuilding",  "modulo(intDiv(Interests, 256), 2)")
	M("HasInterestCulinary",  "modulo(intDiv(Interests, 512), 2)")
	M("OpenstatServiceNameHash","halfMD5(OpenstatServiceName)")
	M("OpenstatCampaignIDHash","halfMD5(OpenstatCampaignID)")
	M("OpenstatAdIDHash",     "halfMD5(OpenstatAdID)")
	M("OpenstatSourceIDHash", "halfMD5(OpenstatSourceID)")
	M("UTMSourceHash",        "halfMD5(UTMSource)")
	M("UTMMediumHash",        "halfMD5(UTMMedium)")
	M("UTMCampaignHash",      "halfMD5(UTMCampaign)")
	M("UTMContentHash",       "halfMD5(UTMContent)")
	M("UTMTermHash",          "halfMD5(UTMTerm)")
	M("FromHash",             "halfMD5(FromTag)")
	M("CLID",                 "CLID")
#undef M
}

void QueryConverter::fillFormattingAggregatedAttributeMap()
{
#define M(a, b) formatting_aggregated_attribute_map[a] = b;
	/*M("Dummy",                "0")
	M("VisitStartDateTime",   "StartTime")
	M("VisitStartDate",       "StartDate")
	M("VisitStartWeek",       "toMonday(StartDate)")
	M("VisitStartTime",       "substring(toString(StartTime), 12, 8)")
	M("VisitStartYear",       "toYear(StartDate)")
	M("VisitStartMonth",      "toMonth(StartDate)")
	M("VisitStartDayOfWeek",  "toDayOfWeek(StartDate)")
	M("VisitStartDayOfMonth", "toDayOfMonth(StartDate)")
	M("VisitStartHour",       "toHour(StartTime)")
	M("VisitStartMinute",     "toMinute(StartTime)")
	M("VisitStartSecond",     "toSecond(StartTime)")
	
	M("FirstVisitDateTime",   "FirstVisit")
	M("FirstVisitDate",       "toDate(FirstVisit)")
	M("FirstVisitWeek",       "toMonday(FirstVisit)")
	M("FirstVisitTime",       "substring(toString(FirstVisit), 12, 8)")
	M("FirstVisitYear",       "toYear(FirstVisit)")
	M("FirstVisitMonth",      "toMonth(FirstVisit)")
	M("FirstVisitDayOfWeek",  "toDayOfWeek(FirstVisit)")
	M("FirstVisitDayOfMonth", "toDayOfMonth(FirstVisit)")
	M("FirstVisitHour",       "toHour(FirstVisit)")
	M("FirstVisitMinute",     "toMinute(FirstVisit)")
	M("FirstVisitSecond",     "toSecond(FirstVisit)")
	
	M("PredLastVisitDate",    "PredLastVisit")
	M("PredLastVisitWeek",    "toMonday(PredLastVisit)")
	M("PredLastVisitYear",    "toYear(PredLastVisit)")
	M("PredLastVisitMonth",   "toMonth(PredLastVisit)")
	M("PredLastVisitDayOfWeek","toDayOfWeek(PredLastVisit)")
	M("PredLastVisitDayOfMonth", "toDayOfMonth(PredLastVisit)")
	
	M("ClientDateTime",       "ClientEventTime")
	M("ClientTime",           "substring(toString(ClientEventTime), 12, 8)")
	M("ClientTimeHour",       "toHour(ClientEventTime)")
	M("ClientTimeMinute",     "toMinute(ClientEventTime)")
	M("ClientTimeSecond",     "toSecond(ClientEventTime)")
	
	M("EndURLHash",           "halfMD5(EndURL)")
	M("RefererHash",          "halfMD5(Referer)")
	M("SearchPhraseHash",     "halfMD5(SearchPhrase)")
	M("RefererDomainHash",    "halfMD5(domainWithoutWWW(Referer))")
	M("StartURLHash",         "halfMD5(StartURL)")
	M("StartURLDomainHash",   "halfMD5(domainWithoutWWW(StartURL))")
	M("RegionID",             "RegionID")
	M("RegionCity",           "regionToCity(RegionID)")
	M("RegionArea",           "regionToArea(RegionID)")
	M("RegionCountry",        "regionToCountry(RegionID)")
	M("TraficSourceID",       "TraficSourceID")
	M("IsNewUser",            "FirstVisit == StartTime")
	M("UserNewness",          "intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400)")
	M("UserNewnessInterval",  "roundToExp2(intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400))")
	M("UserReturnTime",       "toUInt32(toDate(StartTime))-toUInt32(PredLastVisit)")
	M("UserReturnTimeInterval","roundToExp2(toUInt32(toDate(StartTime))-toUInt32(PredLastVisit))")
	M("UserVisitsPeriod",     "(TotalVisits <= 1 ? toUInt16(0) : toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1))))")
	M("UserVisitsPeriodInterval","(TotalVisits <= 1 ? toUInt16(0) : roundToExp2(toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1)))))")
	M("VisitTime",            "Duration")
	M("VisitTimeInterval",    "roundDuration(Duration)")
	M("PageViews",            "PageViews")
	M("PageViewsInterval",    "roundToExp2(PageViews)")
	M("Bounce",               "PageViews <= 1")
	M("BouncePrecise",        "IsBounce")
	M("IsYandex",             "IsYandex")
	M("UserID",               "UserID")
	M("UserIDCreateDateTime", "toDateTime(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000)")
	M("UserIDCreateDate",     "toDate(toDateTime(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000))")
	M("UserIDAge",            "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : intDiv(toInt64(StartTime) - UserID % 10000000000, 86400))")
	M("UserIDAgeInterval",    "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : toInt64(roundToExp2(intDiv(toUInt64(StartTime) - UserID % 10000000000, 86400))))")
	M("TotalVisits",          "TotalVisits")
	M("TotalVisitsInterval",  "roundToExp2(TotalVisits)")
	M("Age",                  "Age")
	M("AgeInterval",          "roundAge(Age)")
	M("Sex",                  "Sex")
	M("Income",               "Income")
	M("AdvEngineID",          "AdvEngineID")
	M("DotNet",               "concat(concat(toString(NetMajor), '.'), toString(NetMinor))")
	M("DotNetMajor",          "NetMajor")
	M("Flash",                "concat(concat(toString(FlashMajor),'.'),toString(FlashMinor))")
	M("FlashExists",          "FlashMajor > 0")
	M("FlashMajor",           "FlashMajor")
	M("Silverlight",          "concat(concat(concat(concat(concat(concat(toString(SilverlightVersion1), '.'), toString(SilverlightVersion2)), '.'), toString(SilverlightVersion3)), '.'), toString(SilverlightVersion4))")
	M("SilverlightMajor",     "SilverlightVersion1")
	M("Hits",                 "Hits")
	M("HitsInterval",         "roundToExp2(Hits)")
	M("JavaEnable",           "JavaEnable")
	M("CookieEnable",         "CookieEnable")
	M("JavascriptEnable",     "JavascriptEnable")
	M("IsMobile",             "IsMobile")
	M("MobilePhoneID",        "MobilePhone")
	M("MobilePhoneModelHash", "halfMD5(MobilePhoneModel)")
	M("MobilePhoneModel",     "MobilePhoneModel")
	M("BrowserLanguage",      "reinterpretAsString(BrowserLanguage)")
	M("BrowserCountry",       "reinterpretAsString(BrowserCountry)")
	M("TopLevelDomain",       "topLevelDomain(StartURL)")
	M("URLScheme",            "protocol(StartURL)")
	M("IPNetworkID",          "IPNetworkID")
	M("ClientTimeZone",       "ClientTimeZone")
	M("OSID",                 "OS")
	M("OSMostAncestor",       "osToRoot(OS)")
	M("ClientIP",             "concat(concat(concat(concat(concat(concat(toString(intDiv(ClientIP, 16777216)),'.'),toString(intDiv(ClientIP, 65536) % 256)),'.'),toString(intDiv(ClientIP, 256) % 256)),'.'),toString(ClientIP % 256))")
	M("Resolution",           "concat(concat(concat(concat(toString(ResolutionWidth),'x'),toString(ResolutionHeight)),'x'),toString(ResolutionDepth))")
	M("ResolutionWidthHeight","concat(concat(toString(ResolutionWidth),'x'),toString(ResolutionHeight))")
	M("ResolutionWidth",      "ResolutionWidth")
	M("ResolutionHeight",     "ResolutionHeight")
	M("ResolutionWidthInterval","intDiv(ResolutionWidth, 100) * 100")
	M("ResolutionHeightInterval","intDiv(ResolutionHeight, 100) * 100")
	M("ResolutionColor",      "ResolutionDepth")
	M("WindowClientArea",     "concat(concat(toString(WindowClientWidth),'x'),toString(WindowClientHeight))")
	M("WindowClientAreaInterval","intDiv(WindowClientWidth, 100) * 6553600 + intDiv(WindowClientHeight, 100) * 100")
	M("WindowClientWidth",    "WindowClientWidth")
	M("WindowClientWidthInterval","intDiv(WindowClientWidth, 100) * 100")
	M("WindowClientHeight",   "WindowClientHeight")
	M("WindowClientHeightInterval","intDiv(WindowClientHeight, 100) * 100")
	M("SearchEngineID",       "SearchEngineID")
	M("SEMostAncestor",       "seToRoot(SearchEngineID)")
	M("CodeVersion",          "CodeVersion")
	M("UserAgent",            "concat(concat(concat(toString(UserAgent), ' '), toString(UserAgentMajor)), UserAgentMinor == 0 ? '' : concat('.', reinterpretAsString(UserAgentMinor)))")
	M("UserAgentVersion",     "concat(toString(UserAgentMajor), UserAgentMinor == 0 ? '' : concat('.', reinterpretAsString(UserAgentMinor)))")
	M("UserAgentMajor",       "concat(concat(toString(UserAgent), ' '), toString(UserAgentMajor))")
	M("UserAgentID",          "UserAgent")
	M("ClickGoodEvent",       "ClickGoodEvent")
	M("ClickPriorityID",      "ClickPriorityID")
	M("ClickBannerID",        "ClickBannerID")
	M("ClickPhraseID",        "ClickPhraseID")
	M("ClickPageID",          "ClickPageID")
	M("ClickPlaceID",         "ClickPlaceID")
	M("ClickTypeID",          "ClickTypeID")
	M("ClickResourceID",      "ClickResourceID")
	M("ClickDomainID",        "ClickDomainID")
	M("ClickCost",            "ClickCost")
	M("ClickURLHash",         "halfMD5(ClickURL)")
	M("ClickOrderID",         "ClickOrderID")
	M("GoalReachesAny",       "")
	M("GoalReachesDepth",     "length(GoalsReached)")
	M("GoalReachesURL",       "")
	M("ConvertedAny",         "")
	M("ConvertedDepth",       "")
	M("ConvertedURL",         "")
	M("GoalReaches",          "arrayCount(GoalsReached, %u)")
	M("Converted",            "has(GoalsReached, %u)")
	M("CounterID",            "CounterID")
	M("VisitID",              "VisitID")
	M("Interests",            "bitmaskToList(Interests)")
	M("HasInterestPhoto",     "modulo(intDiv(Interests, 128), 2)")
	M("HasInterestMoviePremieres","modulo(intDiv(Interests, 64), 2)")
	M("HasInterestTourism",   "modulo(intDiv(Interests, 32), 2)")
	M("HasInterestFamilyAndChildren","modulo(intDiv(Interests, 16), 2)")
	M("HasInterestFinance",   "modulo(intDiv(Interests, 8), 2)")
	M("HasInterestB2B",       "modulo(intDiv(Interests, 4), 2)")
	M("HasInterestCars",      "modulo(intDiv(Interests, 2), 2)")
	M("HasInterestMobileAndInternetCommunications","modulo(Interests, 2)")
	M("HasInterestBuilding",  "modulo(intDiv(Interests, 256), 2)")
	M("HasInterestCulinary",  "modulo(intDiv(Interests, 512), 2)")
	M("OpenstatServiceNameHash","halfMD5(OpenstatServiceName)")
	M("OpenstatCampaignIDHash","halfMD5(OpenstatCampaignID)")
	M("OpenstatAdIDHash",     "halfMD5(OpenstatAdID)")
	M("OpenstatSourceIDHash", "halfMD5(OpenstatSourceID)")
	M("UTMSourceHash",        "halfMD5(UTMSource)")
	M("UTMMediumHash",        "halfMD5(UTMMedium)")
	M("UTMCampaignHash",      "halfMD5(UTMCampaign)")
	M("UTMContentHash",       "halfMD5(UTMContent)")
	M("UTMTermHash",          "halfMD5(UTMTerm)")
	M("FromHash",             "halfMD5(FromTag)")
	M("CLID",                 "CLID")*/
#undef M	
}

void QueryConverter::fillFormattedAttributeMap()
{
#define M(a, b) formatted_attribute_map[a] = b;
	std::string todate = "toDate(toDateTime(%s))";
	std::string todatetime = "toDateTime(%s)";
	std::string cuttime = "substring(toString(toDateTime(%s)), 12, 8)";

	M("VisitStartDateTime",   todatetime)
	M("VisitStartDate",       todate)
	M("VisitStartWeek",       todate)
	M("VisitStartTime",       cuttime)
	
	M("FirstVisitDateTime",   todatetime)
	M("FirstVisitDate",       todate)
	M("FirstVisitWeek",       todate)
	M("FirstVisitTime",       cuttime)
	
	M("PredLastVisitDate",    todate)
	M("PredLastVisitWeek",    todate)
	
	/*
	M("PredLastVisitYear",    "toYear(PredLastVisit)")
	M("PredLastVisitMonth",   "toMonth(PredLastVisit)")
	M("PredLastVisitDayOfWeek","toDayOfWeek(PredLastVisit)")
	M("PredLastVisitDayOfMonth", "toDayOfMonth(PredLastVisit)")
	
	M("ClientDateTime",       "ClientEventTime")
	M("ClientTime",           "substring(toString(ClientEventTime), 12, 8)")
	M("ClientTimeHour",       "toHour(ClientEventTime)")
	M("ClientTimeMinute",     "toMinute(ClientEventTime)")
	M("ClientTimeSecond",     "toSecond(ClientEventTime)")
	
	M("EndURLHash",           "halfMD5(EndURL)")
	M("RefererHash",          "halfMD5(Referer)")
	M("SearchPhraseHash",     "halfMD5(SearchPhrase)")
	M("RefererDomainHash",    "halfMD5(domainWithoutWWW(Referer))")
	M("StartURLHash",         "halfMD5(StartURL)")
	M("StartURLDomainHash",   "halfMD5(domainWithoutWWW(StartURL))")
	M("RegionID",             "RegionID")
	M("RegionCity",           "regionToCity(RegionID)")
	M("RegionArea",           "regionToArea(RegionID)")
	M("RegionCountry",        "regionToCountry(RegionID)")
	M("TraficSourceID",       "TraficSourceID")
	M("IsNewUser",            "FirstVisit == StartTime")
	M("UserNewness",          "intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400)")
	M("UserNewnessInterval",  "roundToExp2(intDiv(toUInt64(StartTime)-toUInt64(FirstVisit), 86400))")
	M("UserReturnTime",       "toUInt32(toDate(StartTime))-toUInt32(PredLastVisit)")
	M("UserReturnTimeInterval","roundToExp2(toUInt32(toDate(StartTime))-toUInt32(PredLastVisit))")
	M("UserVisitsPeriod",     "(TotalVisits <= 1 ? toUInt16(0) : toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1))))")
	M("UserVisitsPeriodInterval","(TotalVisits <= 1 ? toUInt16(0) : roundToExp2(toUInt16((toUInt64(StartTime)-toUInt64(FirstVisit)) / (86400 * (TotalVisits - 1)))))")
	M("VisitTime",            "Duration")
	M("VisitTimeInterval",    "roundDuration(Duration)")
	M("PageViews",            "PageViews")
	M("PageViewsInterval",    "roundToExp2(PageViews)")
	M("Bounce",               "PageViews <= 1")
	M("BouncePrecise",        "IsBounce")
	M("IsYandex",             "IsYandex")
	M("UserID",               "UserID")
	M("UserIDCreateDateTime", "toDateTime(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000)")
	M("UserIDCreateDate",     "toDate(toDateTime(UserID > 10000000000000000000 OR UserID % 10000000000 > 2000000000 OR UserID % 10000000000 < 1000000000 ? toUInt64(0) : UserID % 10000000000))")
	M("UserIDAge",            "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : intDiv(toInt64(StartTime) - UserID % 10000000000, 86400))")
	M("UserIDAgeInterval",    "(UserID > 10000000000000000000 OR UserID % 10000000000 < 1000000000 OR UserID % 10000000000 > toUInt64(StartTime) ? toInt64(-1) : toInt64(roundToExp2(intDiv(toUInt64(StartTime) - UserID % 10000000000, 86400))))")
	M("TotalVisits",          "TotalVisits")
	M("TotalVisitsInterval",  "roundToExp2(TotalVisits)")
	M("Age",                  "Age")
	M("AgeInterval",          "roundAge(Age)")
	M("Sex",                  "Sex")
	M("Income",               "Income")
	M("AdvEngineID",          "AdvEngineID")
	M("DotNet",               "concat(concat(toString(NetMajor), '.'), toString(NetMinor))")
	M("DotNetMajor",          "NetMajor")
	M("Flash",                "concat(concat(toString(FlashMajor),'.'),toString(FlashMinor))")
	M("FlashExists",          "FlashMajor > 0")
	M("FlashMajor",           "FlashMajor")
	M("Silverlight",          "concat(concat(concat(concat(concat(concat(toString(SilverlightVersion1), '.'), toString(SilverlightVersion2)), '.'), toString(SilverlightVersion3)), '.'), toString(SilverlightVersion4))")
	M("SilverlightMajor",     "SilverlightVersion1")
	M("Hits",                 "Hits")
	M("HitsInterval",         "roundToExp2(Hits)")
	M("JavaEnable",           "JavaEnable")
	M("CookieEnable",         "CookieEnable")
	M("JavascriptEnable",     "JavascriptEnable")
	M("IsMobile",             "IsMobile")
	M("MobilePhoneID",        "MobilePhone")
	M("MobilePhoneModelHash", "halfMD5(MobilePhoneModel)")
	M("MobilePhoneModel",     "MobilePhoneModel")
	M("BrowserLanguage",      "reinterpretAsString(BrowserLanguage)")
	M("BrowserCountry",       "reinterpretAsString(BrowserCountry)")
	M("TopLevelDomain",       "topLevelDomain(StartURL)")
	M("URLScheme",            "protocol(StartURL)")
	M("IPNetworkID",          "IPNetworkID")
	M("ClientTimeZone",       "ClientTimeZone")
	M("OSID",                 "OS")
	M("OSMostAncestor",       "osToRoot(OS)")
	M("ClientIP",             "concat(concat(concat(concat(concat(concat(toString(intDiv(ClientIP, 16777216)),'.'),toString(intDiv(ClientIP, 65536) % 256)),'.'),toString(intDiv(ClientIP, 256) % 256)),'.'),toString(ClientIP % 256))")
	M("Resolution",           "concat(concat(concat(concat(toString(ResolutionWidth),'x'),toString(ResolutionHeight)),'x'),toString(ResolutionDepth))")
	M("ResolutionWidthHeight","concat(concat(toString(ResolutionWidth),'x'),toString(ResolutionHeight))")
	M("ResolutionWidth",      "ResolutionWidth")
	M("ResolutionHeight",     "ResolutionHeight")
	M("ResolutionWidthInterval","intDiv(ResolutionWidth, 100) * 100")
	M("ResolutionHeightInterval","intDiv(ResolutionHeight, 100) * 100")
	M("ResolutionColor",      "ResolutionDepth")
	M("WindowClientArea",     "concat(concat(toString(WindowClientWidth),'x'),toString(WindowClientHeight))")
	M("WindowClientAreaInterval","intDiv(WindowClientWidth, 100) * 6553600 + intDiv(WindowClientHeight, 100) * 100")
	M("WindowClientWidth",    "WindowClientWidth")
	M("WindowClientWidthInterval","intDiv(WindowClientWidth, 100) * 100")
	M("WindowClientHeight",   "WindowClientHeight")
	M("WindowClientHeightInterval","intDiv(WindowClientHeight, 100) * 100")
	M("SearchEngineID",       "SearchEngineID")
	M("SEMostAncestor",       "seToRoot(SearchEngineID)")
	M("CodeVersion",          "CodeVersion")
	M("UserAgent",            "concat(concat(concat(toString(UserAgent), ' '), toString(UserAgentMajor)), UserAgentMinor == 0 ? '' : concat('.', reinterpretAsString(UserAgentMinor)))")
	M("UserAgentVersion",     "concat(toString(UserAgentMajor), UserAgentMinor == 0 ? '' : concat('.', reinterpretAsString(UserAgentMinor)))")
	M("UserAgentMajor",       "concat(concat(toString(UserAgent), ' '), toString(UserAgentMajor))")
	M("UserAgentID",          "UserAgent")
	M("ClickGoodEvent",       "ClickGoodEvent")
	M("ClickPriorityID",      "ClickPriorityID")
	M("ClickBannerID",        "ClickBannerID")
	M("ClickPhraseID",        "ClickPhraseID")
	M("ClickPageID",          "ClickPageID")
	M("ClickPlaceID",         "ClickPlaceID")
	M("ClickTypeID",          "ClickTypeID")
	M("ClickResourceID",      "ClickResourceID")
	M("ClickDomainID",        "ClickDomainID")
	M("ClickCost",            "ClickCost")
	M("ClickURLHash",         "halfMD5(ClickURL)")
	M("ClickOrderID",         "ClickOrderID")
	M("GoalReachesAny",       "")
	M("GoalReachesDepth",     "length(GoalsReached)")
	M("GoalReachesURL",       "")
	M("ConvertedAny",         "")
	M("ConvertedDepth",       "")
	M("ConvertedURL",         "")
	M("GoalReaches",          "arrayCount(GoalsReached, %u)")
	M("Converted",            "has(GoalsReached, %u)")
	M("CounterID",            "CounterID")
	M("VisitID",              "VisitID")
	M("Interests",            "bitmaskToList(Interests)")
	M("HasInterestPhoto",     "modulo(intDiv(Interests, 128), 2)")
	M("HasInterestMoviePremieres","modulo(intDiv(Interests, 64), 2)")
	M("HasInterestTourism",   "modulo(intDiv(Interests, 32), 2)")
	M("HasInterestFamilyAndChildren","modulo(intDiv(Interests, 16), 2)")
	M("HasInterestFinance",   "modulo(intDiv(Interests, 8), 2)")
	M("HasInterestB2B",       "modulo(intDiv(Interests, 4), 2)")
	M("HasInterestCars",      "modulo(intDiv(Interests, 2), 2)")
	M("HasInterestMobileAndInternetCommunications","modulo(Interests, 2)")
	M("HasInterestBuilding",  "modulo(intDiv(Interests, 256), 2)")
	M("HasInterestCulinary",  "modulo(intDiv(Interests, 512), 2)")
	M("OpenstatServiceNameHash","halfMD5(OpenstatServiceName)")
	M("OpenstatCampaignIDHash","halfMD5(OpenstatCampaignID)")
	M("OpenstatAdIDHash",     "halfMD5(OpenstatAdID)")
	M("OpenstatSourceIDHash", "halfMD5(OpenstatSourceID)")
	M("UTMSourceHash",        "halfMD5(UTMSource)")
	M("UTMMediumHash",        "halfMD5(UTMMedium)")
	M("UTMCampaignHash",      "halfMD5(UTMCampaign)")
	M("UTMContentHash",       "halfMD5(UTMContent)")
	M("UTMTermHash",          "halfMD5(UTMTerm)")
	M("FromHash",             "halfMD5(FromTag)")
	M("CLID",                 "CLID")*/
#undef M
}

}
}
