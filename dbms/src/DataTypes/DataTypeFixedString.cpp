#include <DB/IO/WriteBuffer.h>
#include <DB/IO/WriteHelpers.h>

#include <DB/Columns/ColumnFixedString.h>
#include <DB/Columns/ColumnsNumber.h>
#include <DB/Columns/ColumnConst.h>

#include <DB/DataTypes/DataTypeFixedString.h>
#include <DB/DataTypes/NullSymbol.h>

#include <DB/IO/ReadHelpers.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/VarInt.h>


namespace DB
{

namespace ErrorCodes
{
	extern const int CANNOT_READ_ALL_DATA;
}


std::string DataTypeFixedString::getName() const
{
	return "FixedString(" + toString(n) + ")";
}


void DataTypeFixedString::serializeBinary(const Field & field, WriteBuffer & ostr) const
{
	const String & s = get<const String &>(field);
	ostr.write(s.data(), std::min(s.size(), n));
	if (s.size() < n)
		for (size_t i = s.size(); i < n; ++i)
			ostr.write(0);
}


void DataTypeFixedString::deserializeBinary(Field & field, ReadBuffer & istr) const
{
	field = String();
	String & s = get<String &>(field);
	s.resize(n);
	istr.readStrict(&s[0], n);
}


void DataTypeFixedString::serializeBinary(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	ostr.write(reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]), n);
}


void DataTypeFixedString::deserializeBinary(IColumn & column, ReadBuffer & istr) const
{
	ColumnFixedString::Chars_t & data = static_cast<ColumnFixedString &>(column).getChars();
	size_t old_size = data.size();
	data.resize(old_size + n);
	try
	{
		istr.readStrict(reinterpret_cast<char *>(&data[old_size]), n);
	}
	catch (...)
	{
		data.resize_assume_reserved(old_size);
		throw;
	}
}


void DataTypeFixedString::serializeBinary(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const
{
	const ColumnFixedString::Chars_t & data = typeid_cast<const ColumnFixedString &>(column).getChars();

	size_t size = data.size() / n;

	if (limit == 0 || offset + limit > size)
		limit = size - offset;

	ostr.write(reinterpret_cast<const char *>(&data[n * offset]), n * limit);
}


void DataTypeFixedString::deserializeBinary(IColumn & column, ReadBuffer & istr, size_t limit, double avg_value_size_hint) const
{
	ColumnFixedString::Chars_t & data = typeid_cast<ColumnFixedString &>(column).getChars();

	size_t initial_size = data.size();
	size_t max_bytes = limit * n;
	data.resize(initial_size + max_bytes);
	size_t read_bytes = istr.readBig(reinterpret_cast<char *>(&data[initial_size]), max_bytes);

	if (read_bytes % n != 0)
		throw Exception("Cannot read all data of type FixedString",
			ErrorCodes::CANNOT_READ_ALL_DATA);

	data.resize(initial_size + read_bytes);
}


void DataTypeFixedString::serializeTextImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::Plain::name, ostr);
	else
		writeString(reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]), n, ostr);
}


void DataTypeFixedString::serializeTextEscapedImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::Escaped::name, ostr);
	else
	{
		const char * pos = reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]);
		writeAnyEscapedString<'\''>(pos, pos + n, ostr);
	}
}

namespace
{

template <typename Reader>
static inline void read(const DataTypeFixedString & self, IColumn & column, Reader && reader)
{
	ColumnFixedString::Chars_t & data = typeid_cast<ColumnFixedString &>(column).getChars();
	size_t prev_size = data.size();

	try
	{
		reader(data);
	}
	catch (...)
	{
		data.resize_assume_reserved(prev_size);
		throw;
	}

	if (data.size() < prev_size + self.getN())
		data.resize_fill(prev_size + self.getN());

	if (data.size() > prev_size + self.getN())
	{
		data.resize_assume_reserved(prev_size);
		throw Exception("Too large value for " + self.getName(), ErrorCodes::TOO_LARGE_STRING_SIZE);
	}
}

void insertEmptyString(const DataTypeFixedString & self, IColumn & column)
{
	ColumnFixedString::Chars_t & data = typeid_cast<ColumnFixedString &>(column).getChars();
	size_t prev_size = data.size();
	data.resize_fill(prev_size + self.getN());
}

}

void DataTypeFixedString::deserializeTextEscapedImpl(IColumn & column, ReadBuffer & istr,
	NullValuesByteMap * null_map) const
{
	if (NullSymbol::Deserializer<NullSymbol::Escaped>::execute(column, istr, null_map))
		insertEmptyString(*this, column);
	else
		read(*this, column, [&istr](ColumnFixedString::Chars_t & data) { readEscapedStringInto(data, istr); });
}


void DataTypeFixedString::serializeTextQuotedImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::Quoted::name, ostr);
	else
	{
		const char * pos = reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]);
		writeAnyQuotedString<'\''>(pos, pos + n, ostr);
	}
}


void DataTypeFixedString::deserializeTextQuotedImpl(IColumn & column, ReadBuffer & istr,
	NullValuesByteMap * null_map) const
{
	if (NullSymbol::Deserializer<NullSymbol::Quoted>::execute(column, istr, null_map))
		insertEmptyString(*this, column);
	else
		read(*this, column, [&istr](ColumnFixedString::Chars_t & data) { readQuotedStringInto(data, istr); });
}


void DataTypeFixedString::serializeTextJSONImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::JSON::name, ostr);
	else
	{
		const char * pos = reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]);
		writeJSONString(pos, pos + n, ostr);
	}
}


void DataTypeFixedString::deserializeTextJSONImpl(IColumn & column, ReadBuffer & istr,
	NullValuesByteMap * null_map) const
{
	if (NullSymbol::Deserializer<NullSymbol::JSON>::execute(column, istr, null_map))
		insertEmptyString(*this, column);
	else
		read(*this, column, [&istr](ColumnFixedString::Chars_t & data) { readJSONStringInto(data, istr); });
}


void DataTypeFixedString::serializeTextXMLImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::XML::name, ostr);
	else
	{
		const char * pos = reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]);
		writeXMLString(pos, pos + n, ostr);
	}
}


void DataTypeFixedString::serializeTextCSVImpl(const IColumn & column, size_t row_num, WriteBuffer & ostr,
	const NullValuesByteMap * null_map) const
{
	if (isNullValue(null_map, row_num))
		writeCString(NullSymbol::CSV::name, ostr);
	else
	{
		const char * pos = reinterpret_cast<const char *>(&static_cast<const ColumnFixedString &>(column).getChars()[n * row_num]);
		writeCSVString(pos, pos + n, ostr);
	}
}


void DataTypeFixedString::deserializeTextCSVImpl(IColumn & column, ReadBuffer & istr, const char delimiter,
	NullValuesByteMap * null_map) const
{
	if (NullSymbol::Deserializer<NullSymbol::CSV>::execute(column, istr, null_map))
		insertEmptyString(*this, column);
	else
		read(*this, column, [&istr](ColumnFixedString::Chars_t & data) { readCSVStringInto(data, istr); });
}


ColumnPtr DataTypeFixedString::createColumn() const
{
	return std::make_shared<ColumnFixedString>(n);
}


ColumnPtr DataTypeFixedString::createConstColumn(size_t size, const Field & field) const
{
	return std::make_shared<ColumnConstString>(size, get<const String &>(field), clone());
}

}
