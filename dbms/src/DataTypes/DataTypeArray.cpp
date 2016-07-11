#include <DB/Columns/ColumnArray.h>
#include <DB/Columns/ColumnConst.h>

#include <DB/IO/ReadHelpers.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/ReadBufferFromString.h>

#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/DataTypes/DataTypeArray.h>


namespace DB
{

namespace ErrorCodes
{
	extern const int CANNOT_READ_ARRAY_FROM_TEXT;
}


DataTypeArray::DataTypeArray(DataTypePtr nested_)
	: enriched_nested(std::make_pair(nested_, std::make_shared<DataTypeVoid>())), nested{nested_}
{
	offsets = std::make_shared<DataTypeFromFieldType<ColumnArray::Offset_t>::Type>();
}

DataTypeArray::DataTypeArray(DataTypeTraits::EnrichedDataTypePtr enriched_nested_)
	: enriched_nested{enriched_nested_}, nested{enriched_nested.first}
{
	offsets = std::make_shared<DataTypeFromFieldType<ColumnArray::Offset_t>::Type>();
}

void DataTypeArray::serializeBinary(const Field & field, WriteBuffer & ostr) const
{
	const Array & a = get<const Array &>(field);
	writeVarUInt(a.size(), ostr);
	for (size_t i = 0; i < a.size(); ++i)
	{
		nested->serializeBinary(a[i], ostr);
	}
}


void DataTypeArray::deserializeBinary(Field & field, ReadBuffer & istr) const
{
	size_t size;
	readVarUInt(size, istr);
	field = Array(size);
	Array & arr = get<Array &>(field);
	for (size_t i = 0; i < size; ++i)
		nested->deserializeBinary(arr[i], istr);
}


void DataTypeArray::serializeBinary(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	const ColumnArray & column_array = static_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	size_t offset = row_num == 0 ? 0 : offsets[row_num - 1];
	size_t next_offset = offsets[row_num];
	size_t size = next_offset - offset;

	writeVarUInt(size, ostr);

	const IColumn & nested_column = column_array.getData();
	for (size_t i = offset; i < next_offset; ++i)
		nested->serializeBinary(nested_column, i, ostr);
}


void DataTypeArray::deserializeBinary(IColumn & column, ReadBuffer & istr) const
{
	ColumnArray & column_array = static_cast<ColumnArray &>(column);
	ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	size_t size;
	readVarUInt(size, istr);

	IColumn & nested_column = column_array.getData();

	size_t i = 0;
	try
	{
		for (; i < size; ++i)
			nested->deserializeBinary(nested_column, istr);
	}
	catch (...)
	{
		if (i)
			nested_column.popBack(i);
		throw;
	}

	offsets.push_back((offsets.empty() ? 0 : offsets.back()) + size);
}


void DataTypeArray::serializeBinary(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const
{
	const ColumnArray & column_array = typeid_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	if (offset > offsets.size())
		return;

	/** offset - с какого массива писать.
	  * limit - сколько массивов максимум записать, или 0, если писать всё, что есть.
	  * end - до какого массива заканчивается записываемый кусок.
	  *
	  * nested_offset - с какого элемента внутренностей писать.
	  * nested_limit - сколько элементов внутренностей писать, или 0, если писать всё, что есть.
	  */

	size_t end = std::min(offset + limit, offsets.size());

	size_t nested_offset = offset ? offsets[offset - 1] : 0;
	size_t nested_limit = limit
		? offsets[end - 1] - nested_offset
		: 0;

	if (limit == 0 || nested_limit)
		nested->serializeBinary(column_array.getData(), ostr, nested_offset, nested_limit);
}


void DataTypeArray::deserializeBinary(IColumn & column, ReadBuffer & istr, size_t limit, double avg_value_size_hint) const
{
	ColumnArray & column_array = typeid_cast<ColumnArray &>(column);
	ColumnArray::Offsets_t & offsets = column_array.getOffsets();
	IColumn & nested_column = column_array.getData();

	/// Должно быть считано согласнованное с offsets количество значений.
	size_t last_offset = (offsets.empty() ? 0 : offsets.back());
	if (last_offset < nested_column.size())
		throw Exception("Nested column longer than last offset", ErrorCodes::LOGICAL_ERROR);
	size_t nested_limit = last_offset - nested_column.size();
	nested->deserializeBinary(nested_column, istr, nested_limit, 0);

	if (column_array.getData().size() != last_offset)
		throw Exception("Cannot read all array values", ErrorCodes::CANNOT_READ_ALL_DATA);
}


void DataTypeArray::serializeOffsets(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const
{
	const ColumnArray & column_array = typeid_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();
	size_t size = offsets.size();

	if (!size)
		return;

	size_t end = limit && (offset + limit < size)
		? offset + limit
		: size;

	if (offset == 0)
	{
		writeIntBinary(offsets[0], ostr);
		++offset;
	}

	for (size_t i = offset; i < end; ++i)
		writeIntBinary(offsets[i] - offsets[i - 1], ostr);
}


void DataTypeArray::deserializeOffsets(IColumn & column, ReadBuffer & istr, size_t limit) const
{
	ColumnArray & column_array = typeid_cast<ColumnArray &>(column);
	ColumnArray::Offsets_t & offsets = column_array.getOffsets();
	size_t initial_size = offsets.size();
	offsets.resize(initial_size + limit);

	size_t i = initial_size;
	ColumnArray::Offset_t current_offset = initial_size ? offsets[initial_size - 1] : 0;
	while (i < initial_size + limit && !istr.eof())
	{
		ColumnArray::Offset_t current_size = 0;
		readIntBinary(current_size, istr);
		current_offset += current_size;
		offsets[i] = current_offset;
		++i;
	}

	offsets.resize(i);
}

void DataTypeArray::serializeTextInternal(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	const ColumnArray & column_array = static_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	size_t offset = row_num == 0 ? 0 : offsets[row_num - 1];
	size_t next_offset = offsets[row_num];

	const IColumn & nested_column = column_array.getData();

	writeChar('[', ostr);
	for (size_t i = offset; i < next_offset; ++i)
	{
		if (i != offset)
			writeChar(',', ostr);
		nested->serializeTextQuoted(nested_column, i, ostr);
	}
	writeChar(']', ostr);
}

void DataTypeArray::serializeText(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	serializeTextInternal(column, row_num, ostr);
}

namespace
{

template <typename Reader>
void deserializeTextInternal(IColumn & column, ReadBuffer & istr, Reader && read_nested)
{
	ColumnArray & column_array = static_cast<ColumnArray &>(column);
	ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	IColumn & nested_column = column_array.getData();

	size_t size = 0;
	bool first = true;
	assertChar('[', istr);

	try
	{
		while (!istr.eof() && *istr.position() != ']')
		{
			if (!first)
			{
				if (*istr.position() == ',')
					++istr.position();
				else
					throw Exception("Cannot read array from text", ErrorCodes::CANNOT_READ_ARRAY_FROM_TEXT);
			}

			first = false;

			skipWhitespaceIfAny(istr);

			if (*istr.position() == ']')
				break;

			read_nested(nested_column);
			++size;

			skipWhitespaceIfAny(istr);
		}
		assertChar(']', istr);
	}
	catch (...)
	{
		nested_column.popBack(size);
		throw;
	}

	offsets.push_back((offsets.empty() ? 0 : offsets.back()) + size);
}

}

void DataTypeArray::deserializeTextQuotedInternal(IColumn & column, ReadBuffer & istr) const
{
	deserializeTextInternal(column, istr, [&](IColumn & nested_column) { nested->deserializeTextQuoted(nested_column, istr); });
}

void DataTypeArray::deserializeText(IColumn & column, ReadBuffer & istr) const
{
	deserializeTextQuotedInternal(column, istr);
}


void DataTypeArray::serializeTextEscaped(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	serializeTextInternal(column, row_num, ostr);
}


void DataTypeArray::deserializeTextEscaped(IColumn & column, ReadBuffer & istr) const
{
	deserializeText(column, istr);
}


void DataTypeArray::serializeTextQuoted(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	serializeTextInternal(column, row_num, ostr);
}


void DataTypeArray::deserializeTextQuoted(IColumn & column, ReadBuffer & istr) const
{
	deserializeText(column, istr);
}


void DataTypeArray::serializeTextJSON(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	const ColumnArray & column_array = static_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	size_t offset = row_num == 0 ? 0 : offsets[row_num - 1];
	size_t next_offset = offsets[row_num];

	const IColumn & nested_column = column_array.getData();

	writeChar('[', ostr);
	for (size_t i = offset; i < next_offset; ++i)
	{
		if (i != offset)
			writeChar(',', ostr);
		nested->serializeTextJSON(nested_column, i, ostr);
	}
	writeChar(']', ostr);
}


void DataTypeArray::deserializeTextJSON(IColumn & column, ReadBuffer & istr) const
{
	deserializeTextInternal(column, istr, [&](IColumn & nested_column) { nested->deserializeTextJSON(nested_column, istr); });
}


void DataTypeArray::serializeTextXML(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	const ColumnArray & column_array = static_cast<const ColumnArray &>(column);
	const ColumnArray::Offsets_t & offsets = column_array.getOffsets();

	size_t offset = row_num == 0 ? 0 : offsets[row_num - 1];
	size_t next_offset = offsets[row_num];

	const IColumn & nested_column = column_array.getData();

	writeCString("<array>", ostr);
	for (size_t i = offset; i < next_offset; ++i)
	{
		writeCString("<elem>", ostr);
		nested->serializeTextXML(nested_column, i, ostr);
		writeCString("</elem>", ostr);
	}
	writeCString("</array>", ostr);
}


void DataTypeArray::serializeTextCSV(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	/// Хорошего способа сериализовать массив в CSV нет. Поэтому сериализуем его в строку, а затем полученную строку запишем в CSV.
	String s;
	{
		WriteBufferFromString wb(s);
		serializeTextInternal(column, row_num, wb);
	}
	writeCSV(s, ostr);
}


void DataTypeArray::deserializeTextCSV(IColumn & column, ReadBuffer & istr, const char delimiter) const
{
	String s;
	readCSV(s, istr, delimiter);
	ReadBufferFromString rb(s);
	deserializeTextQuotedInternal(column, rb);
}


ColumnPtr DataTypeArray::createColumn() const
{
	return std::make_shared<ColumnArray>(nested->createColumn());
}


ColumnPtr DataTypeArray::createConstColumn(size_t size, const Field & field) const
{
	/// Последним аргументом нельзя отдать this.
	return std::make_shared<ColumnConstArray>(size, get<const Array &>(field), std::make_shared<DataTypeArray>(nested));
}

}
