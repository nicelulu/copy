#include <DB/IO/WriteHelpers.h>
#include <DB/IO/ReadHelpers.h>

#include <DB/Columns/ColumnConst.h>
#include <DB/Columns/ColumnAggregateFunction.h>

#include <DB/DataTypes/DataTypeAggregateFunction.h>


namespace DB
{

using Poco::SharedPtr;


void DataTypeAggregateFunction::serializeBinary(const Field & field, WriteBuffer & ostr) const
{
	const String & s = get<const String &>(field);
	writeVarUInt(s.size(), ostr);
	writeString(s, ostr);
}

void DataTypeAggregateFunction::deserializeBinary(Field & field, ReadBuffer & istr) const
{
	UInt64 size;
	readVarUInt(size, istr);
	field = String();
	String & s = get<String &>(field);
	s.resize(size);
	istr.readStrict(&s[0], size);
}

void DataTypeAggregateFunction::serializeBinary(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const
{
	const ColumnAggregateFunction & real_column = dynamic_cast<const ColumnAggregateFunction &>(column);
	const ColumnAggregateFunction::Container_t & vec = real_column.getData();

	ColumnAggregateFunction::Container_t::const_iterator it = vec.begin() + offset;
	ColumnAggregateFunction::Container_t::const_iterator end = limit ? it + limit : vec.end();

	if (end > vec.end())
		end = vec.end();

	for (; it != end; ++it)
		function->serialize(*it, ostr);
}

void DataTypeAggregateFunction::deserializeBinary(IColumn & column, ReadBuffer & istr, size_t limit) const
{
	ColumnAggregateFunction & real_column = dynamic_cast<ColumnAggregateFunction &>(column);
	ColumnAggregateFunction::Container_t & vec = real_column.getData();

	Arena * arena = new Arena;
	real_column.set(function);
	real_column.addArena(arena);
	vec.reserve(vec.size() + limit);

	size_t size_of_state = function->sizeOfData();

	for (size_t i = 0; i < limit; ++i)
	{
		if (istr.eof())
			break;

		AggregateDataPtr place = arena->alloc(size_of_state);

		function->create(place);
		function->deserializeMerge(place, istr);
		
		vec.push_back(place);
	}
}

void DataTypeAggregateFunction::serializeText(const Field & field, WriteBuffer & ostr) const
{
	writeString(get<const String &>(field), ostr);
}


void DataTypeAggregateFunction::deserializeText(Field & field, ReadBuffer & istr) const
{
	field.assignString("", 0);
	readString(get<String &>(field), istr);
}


void DataTypeAggregateFunction::serializeTextEscaped(const Field & field, WriteBuffer & ostr) const
{
	writeEscapedString(get<const String &>(field), ostr);
}


void DataTypeAggregateFunction::deserializeTextEscaped(Field & field, ReadBuffer & istr) const
{
	field.assignString("", 0);
	readEscapedString(get<String &>(field), istr);
}


void DataTypeAggregateFunction::serializeTextQuoted(const Field & field, WriteBuffer & ostr) const
{
	writeQuotedString(get<const String &>(field), ostr);
}


void DataTypeAggregateFunction::deserializeTextQuoted(Field & field, ReadBuffer & istr) const
{
	field.assignString("", 0);
	readQuotedString(get<String &>(field), istr);
}


void DataTypeAggregateFunction::serializeTextJSON(const Field & field, WriteBuffer & ostr) const
{
	writeJSONString(get<const String &>(field), ostr);
}

ColumnPtr DataTypeAggregateFunction::createColumn() const
{
	return new ColumnAggregateFunction(new AggregateStatesHolder(function));
}

ColumnPtr DataTypeAggregateFunction::createConstColumn(size_t size, const Field & field) const
{
	throw Exception("Const column with aggregate function is not supported", ErrorCodes::NOT_IMPLEMENTED);
}


}

