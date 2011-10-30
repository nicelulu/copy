#include <iostream>
#include <iomanip>

#include <boost/assign/list_inserter.hpp>

#include <Poco/SharedPtr.h>
#include <Poco/Stopwatch.h>
#include <Poco/NumberParser.h>

#include <DB/IO/WriteBufferFromOStream.h>

#include <DB/Storages/StorageLog.h>
#include <DB/Storages/StorageSystemNumbers.h>
#include <DB/Storages/StorageSystemOne.h>

#include <DB/DataStreams/TabSeparatedRowOutputStream.h>
#include <DB/DataStreams/copyData.h>

#include <DB/DataTypes/DataTypesNumberFixed.h>

#include <DB/Functions/FunctionsArithmetic.h>
#include <DB/Functions/FunctionsComparison.h>
#include <DB/Functions/FunctionsLogical.h>
#include <DB/Functions/FunctionsString.h>
#include <DB/Functions/FunctionsConversion.h>
#include <DB/Functions/FunctionsDateTime.h>
#include <DB/Functions/FunctionsStringSearch.h>

#include <DB/Parsers/ParserSelectQuery.h>
#include <DB/Parsers/formatAST.h>

#include <DB/Interpreters/InterpreterSelectQuery.h>


using Poco::SharedPtr;


int main(int argc, char ** argv)
{
	try
	{
		/// Заранее инициализируем DateLUT, чтобы первая инициализация потом не влияла на измеряемую скорость выполнения.
		Yandex::DateLUTSingleton::instance();
		
		typedef std::pair<std::string, SharedPtr<DB::IDataType> > NameAndTypePair;
		typedef std::list<NameAndTypePair> NamesAndTypesList;

		NamesAndTypesList names_and_types_list;

		boost::assign::push_back(names_and_types_list)
			("WatchID",				new DB::DataTypeUInt64)
			("JavaEnable",			new DB::DataTypeUInt8)
			("Title",				new DB::DataTypeString)
			("GoodEvent",			new DB::DataTypeUInt32)
			("EventTime",			new DB::DataTypeDateTime)
			("CounterID",			new DB::DataTypeUInt32)
			("ClientIP",			new DB::DataTypeUInt32)
			("RegionID",			new DB::DataTypeUInt32)
			("UniqID",				new DB::DataTypeUInt64)
			("CounterClass",		new DB::DataTypeUInt8)
			("OS",					new DB::DataTypeUInt8)
			("UserAgent",			new DB::DataTypeUInt8)
			("URL",					new DB::DataTypeString)
			("Referer",				new DB::DataTypeString)
			("Refresh",				new DB::DataTypeUInt8)
			("ResolutionWidth",		new DB::DataTypeUInt16)
			("ResolutionHeight",	new DB::DataTypeUInt16)
			("ResolutionDepth",		new DB::DataTypeUInt8)
			("FlashMajor",			new DB::DataTypeUInt8)
			("FlashMinor",			new DB::DataTypeUInt8)
			("FlashMinor2",			new DB::DataTypeString)
			("NetMajor",			new DB::DataTypeUInt8)
			("NetMinor",			new DB::DataTypeUInt8)
			("UserAgentMajor",		new DB::DataTypeUInt16)
			("UserAgentMinor",		new DB::DataTypeFixedString(2))
			("CookieEnable",		new DB::DataTypeUInt8)
			("JavascriptEnable",	new DB::DataTypeUInt8)
			("IsMobile",			new DB::DataTypeUInt8)
			("MobilePhone",			new DB::DataTypeUInt8)
			("MobilePhoneModel",	new DB::DataTypeString)
			("Params",				new DB::DataTypeString)
			("IPNetworkID",			new DB::DataTypeUInt32)
			("TraficSourceID",		new DB::DataTypeInt8)
			("SearchEngineID",		new DB::DataTypeUInt16)
			("SearchPhrase",		new DB::DataTypeString)
			("AdvEngineID",			new DB::DataTypeUInt8)
			("IsArtifical",			new DB::DataTypeUInt8)
			("WindowClientWidth",	new DB::DataTypeUInt16)
			("WindowClientHeight",	new DB::DataTypeUInt16)
			("ClientTimeZone",		new DB::DataTypeInt16)
			("ClientEventTime",		new DB::DataTypeDateTime)
			("SilverlightVersion1",	new DB::DataTypeUInt8)
			("SilverlightVersion2",	new DB::DataTypeUInt8)
			("SilverlightVersion3",	new DB::DataTypeUInt32)
			("SilverlightVersion4",	new DB::DataTypeUInt16)
			("PageCharset",			new DB::DataTypeString)
			("CodeVersion",			new DB::DataTypeUInt32)
			("IsLink",				new DB::DataTypeUInt8)
			("IsDownload",			new DB::DataTypeUInt8)
			("IsNotBounce",			new DB::DataTypeUInt8)
			("FUniqID",				new DB::DataTypeUInt64)
			("OriginalURL",			new DB::DataTypeString)
			("HID",					new DB::DataTypeUInt32)
			("IsOldCounter",		new DB::DataTypeUInt8)
			("IsEvent",				new DB::DataTypeUInt8)
			("IsParameter",			new DB::DataTypeUInt8)
			("DontCountHits",		new DB::DataTypeUInt8)
			("WithHash",			new DB::DataTypeUInt8)
		;

		SharedPtr<DB::NamesAndTypes> names_and_types_map = new DB::NamesAndTypes;

		for (NamesAndTypesList::const_iterator it = names_and_types_list.begin(); it != names_and_types_list.end(); ++it)
		{
			names_and_types_map->insert(*it);
		}

		DB::Context context;

		boost::assign::insert(*context.functions)
			("plus",			new DB::FunctionPlus)
			("minus",			new DB::FunctionMinus)
			("multiply",		new DB::FunctionMultiply)
			("divide",			new DB::FunctionDivideFloating)
			("intDiv",			new DB::FunctionDivideIntegral)
			("modulo",			new DB::FunctionModulo)
			("negate",			new DB::FunctionNegate)

			("equals",			new DB::FunctionEquals)
			("notEquals",		new DB::FunctionNotEquals)
			("less",			new DB::FunctionLess)
			("greater",			new DB::FunctionGreater)
			("lessOrEquals",	new DB::FunctionLessOrEquals)
			("greaterOrEquals",	new DB::FunctionGreaterOrEquals)

			("and",				new DB::FunctionAnd)
			("or",				new DB::FunctionOr)
			("xor",				new DB::FunctionXor)
			("not",				new DB::FunctionNot)

			("length",			new DB::FunctionLength)
			("lengthUTF8",		new DB::FunctionLengthUTF8)
			("lower",			new DB::FunctionLower)
			("upper",			new DB::FunctionUpper)
			("lowerUTF8",		new DB::FunctionLowerUTF8)
			("upperUTF8",		new DB::FunctionUpperUTF8)
			("reverse",			new DB::FunctionReverse)
			("reverseUTF8",		new DB::FunctionReverseUTF8)
			("concat",			new DB::FunctionConcat)
			("substring",		new DB::FunctionSubstring)
			("substringUTF8",	new DB::FunctionSubstringUTF8)

			("toUInt8",			new DB::FunctionToUInt8)
			("toUInt16",		new DB::FunctionToUInt16)
			("toUInt32",		new DB::FunctionToUInt32)
			("toUInt64",		new DB::FunctionToUInt64)
			("toInt8",			new DB::FunctionToInt8)
			("toInt16",			new DB::FunctionToInt16)
			("toInt32",			new DB::FunctionToInt32)
			("toInt64",			new DB::FunctionToInt64)
			("toFloat32",		new DB::FunctionToFloat32)
			("toFloat64",		new DB::FunctionToFloat64)
			("toVarUInt",		new DB::FunctionToVarUInt)
			("toVarInt",		new DB::FunctionToVarInt)
			("toDate",			new DB::FunctionToDate)
			("toDateTime",		new DB::FunctionToDateTime)
			("toString",		new DB::FunctionToString)

			("toYear",			new DB::FunctionToYear)
			("toMonth",			new DB::FunctionToMonth)
			("toDayOfMonth",	new DB::FunctionToDayOfMonth)
			("toDayOfWeek",		new DB::FunctionToDayOfWeek)
			("toHour",			new DB::FunctionToHour)
			("toMinute",		new DB::FunctionToMinute)
			("toSecond",		new DB::FunctionToSecond)
			("toMonday",		new DB::FunctionToMonday)
			("toStartOfMonth",	new DB::FunctionToStartOfMonth)
			("toTime",			new DB::FunctionToTime)

			("position",		new DB::FunctionPosition)
			("positionUTF8",	new DB::FunctionPositionUTF8)
			("match",			new DB::FunctionMatch)
			("like",			new DB::FunctionLike)
			("notLike",			new DB::FunctionNotLike)
		;

		context.aggregate_function_factory		= new DB::AggregateFunctionFactory;

		(*context.databases)["default"]["hits"] 	= new DB::StorageLog("./", "hits", names_and_types_map, ".bin");
		(*context.databases)["default"]["hits2"] 	= new DB::StorageLog("./", "hits2", names_and_types_map, ".bin");
		(*context.databases)["default"]["hits3"] 	= new DB::StorageLog("./", "hits3", names_and_types_map, ".bin");
		(*context.databases)["system"]["one"] 		= new DB::StorageSystemOne("one");
		(*context.databases)["system"]["numbers"] 	= new DB::StorageSystemNumbers("numbers");
		context.current_database = "default";

		DB::ParserSelectQuery parser;
		DB::ASTPtr ast;
		std::string input;/* =
			"SELECT "
			"	count(),"
			"	UniqID % 100,"
			"	UniqID % 100 * 2,"
			"	-1,"
			"	count(),"
			"	count() * count(),"
			"	sum(-OS + UserAgent + TraficSourceID + SearchEngineID) + 101,"
			"	SearchPhrase"
			"FROM hits "
			"WHERE SearchPhrase != '' "
			"GROUP BY UniqID % 100, SearchPhrase "
			"ORDER BY count() DESC "
			"LIMIT 20";*/
		std::stringstream str;
		str << std::cin.rdbuf();
		input = str.str();
		
		std::string expected;

		const char * begin = input.data();
		const char * end = begin + input.size();
		const char * pos = begin;

		bool parse_res = parser.parse(pos, end, ast, expected);

		if (!parse_res || pos != end)
			throw DB::Exception("Syntax error: failed at position "
				+ Poco::NumberFormatter::format(pos - begin) + ": "
				+ input.substr(pos - begin, 10)
				+ ", expected " + (parse_res ? "end of data" : expected) + ".",
				DB::ErrorCodes::SYNTAX_ERROR);

		DB::formatAST(*ast, std::cerr);
		std::cerr << std::endl;
/*		std::cerr << ast->getTreeID() << std::endl;
*/
		DB::WriteBufferFromOStream ob(std::cout);
		DB::InterpreterSelectQuery interpreter(ast, context);
		DB::BlockInputStreamPtr stream = interpreter.executeAndFormat(ob);

		std::cerr << std::endl;
		stream->dumpTree(std::cerr);
	}
	catch (const DB::Exception & e)
	{
		std::cerr << e.what() << ", " << e.message() << std::endl;
		return 1;
	}

	return 0;
}
