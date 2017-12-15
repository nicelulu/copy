#include <Columns/ColumnTuple.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypeFactory.h>
#include <Parsers/IAST.h>
#include <IO/WriteHelpers.h>
#include <IO/ReadHelpers.h>

#include <ext/map.h>
#include <ext/enumerate.h>
#include <ext/range.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int EMPTY_DATA_PASSED;
}


std::string DataTypeTuple::getName() const
{
    std::stringstream s;

    s << "Tuple(";
    for (DataTypes::const_iterator it = elems.begin(); it != elems.end(); ++it)
        s << (it == elems.begin() ? "" : ", ") << (*it)->getName();
    s << ")";

    return s.str();
}


static inline IColumn & extractElementColumn(IColumn & column, size_t idx)
{
    return *static_cast<ColumnTuple &>(column).getColumnPtr(idx);
}

static inline const IColumn & extractElementColumn(const IColumn & column, size_t idx)
{
    return *static_cast<const ColumnTuple &>(column).getColumnPtr(idx);
}


void DataTypeTuple::serializeBinary(const Field & field, WriteBuffer & ostr) const
{
    const auto & tuple = get<const Tuple &>(field).t;
    for (const auto & idx_elem : ext::enumerate(elems))
        idx_elem.second->serializeBinary(tuple[idx_elem.first], ostr);
}

void DataTypeTuple::deserializeBinary(Field & field, ReadBuffer & istr) const
{
    const size_t size = elems.size();
    field = Tuple(TupleBackend(size));
    TupleBackend & tuple = get<Tuple &>(field).t;
    for (const auto i : ext::range(0, size))
        elems[i]->deserializeBinary(tuple[i], istr);
}

void DataTypeTuple::serializeBinary(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    for (const auto & idx_elem : ext::enumerate(elems))
        idx_elem.second->serializeBinary(extractElementColumn(column, idx_elem.first), row_num, ostr);
}


template <typename F>
static void addElementSafe(const DataTypes & elems, IColumn & column, F && impl)
{
    /// We use the assumption that tuples of zero size do not exist.
    size_t old_size = column.size();

    try
    {
        impl();
    }
    catch (...)
    {
        for (const auto & i : ext::range(0, ext::size(elems)))
        {
            auto & element_column = extractElementColumn(column, i);
            if (element_column.size() > old_size)
                element_column.popBack(1);
        }

        throw;
    }
}


void DataTypeTuple::deserializeBinary(IColumn & column, ReadBuffer & istr) const
{
    addElementSafe(elems, column, [&]
    {
        for (const auto & i : ext::range(0, ext::size(elems)))
            elems[i]->deserializeBinary(extractElementColumn(column, i), istr);
    });
}

void DataTypeTuple::serializeText(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    writeChar('(', ostr);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        if (i != 0)
            writeChar(',', ostr);
        elems[i]->serializeTextQuoted(extractElementColumn(column, i), row_num, ostr);
    }
    writeChar(')', ostr);
}

void DataTypeTuple::deserializeText(IColumn & column, ReadBuffer & istr) const
{
    const size_t size = elems.size();
    assertChar('(', istr);

    addElementSafe(elems, column, [&]
    {
        for (const auto i : ext::range(0, size))
        {
            skipWhitespaceIfAny(istr);
            if (i != 0)
            {
                assertChar(',', istr);
                skipWhitespaceIfAny(istr);
            }
            elems[i]->deserializeTextQuoted(extractElementColumn(column, i), istr);
        }
    });

    skipWhitespaceIfAny(istr);
    assertChar(')', istr);
}

void DataTypeTuple::serializeTextEscaped(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    serializeText(column, row_num, ostr);
}

void DataTypeTuple::deserializeTextEscaped(IColumn & column, ReadBuffer & istr) const
{
    deserializeText(column, istr);
}

void DataTypeTuple::serializeTextQuoted(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    serializeText(column, row_num, ostr);
}

void DataTypeTuple::deserializeTextQuoted(IColumn & column, ReadBuffer & istr) const
{
    deserializeText(column, istr);
}

void DataTypeTuple::serializeTextJSON(const IColumn & column, size_t row_num, WriteBuffer & ostr, const FormatSettingsJSON & settings) const
{
    writeChar('[', ostr);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        if (i != 0)
            writeChar(',', ostr);
        elems[i]->serializeTextJSON(extractElementColumn(column, i), row_num, ostr, settings);
    }
    writeChar(']', ostr);
}

void DataTypeTuple::deserializeTextJSON(IColumn & column, ReadBuffer & istr) const
{
    const size_t size = elems.size();
    assertChar('[', istr);

    addElementSafe(elems, column, [&]
    {
        for (const auto i : ext::range(0, size))
        {
            skipWhitespaceIfAny(istr);
            if (i != 0)
            {
                assertChar(',', istr);
                skipWhitespaceIfAny(istr);
            }
            elems[i]->deserializeTextJSON(extractElementColumn(column, i), istr);
        }
    });

    skipWhitespaceIfAny(istr);
    assertChar(']', istr);
}

void DataTypeTuple::serializeTextXML(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    writeCString("<tuple>", ostr);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        writeCString("<elem>", ostr);
        elems[i]->serializeTextXML(extractElementColumn(column, i), row_num, ostr);
        writeCString("</elem>", ostr);
    }
    writeCString("</tuple>", ostr);
}

void DataTypeTuple::serializeTextCSV(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        if (i != 0)
            writeChar(',', ostr);
        elems[i]->serializeTextCSV(extractElementColumn(column, i), row_num, ostr);
    }
}

void DataTypeTuple::deserializeTextCSV(IColumn & column, ReadBuffer & istr, const char delimiter) const
{
    addElementSafe(elems, column, [&]
    {
        const size_t size = elems.size();
        for (const auto i : ext::range(0, size))
        {
            if (i != 0)
            {
                skipWhitespaceIfAny(istr);
                assertChar(delimiter, istr);
                skipWhitespaceIfAny(istr);
            }
            elems[i]->deserializeTextCSV(extractElementColumn(column, i), istr, delimiter);
        }
    });
}

void DataTypeTuple::enumerateStreams(StreamCallback callback, SubstreamPath path) const
{
    path.push_back(Substream::TupleElement);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        path.back().tuple_element = i + 1;
        elems[i]->enumerateStreams(callback, path);
    }
}

void DataTypeTuple::serializeBinaryBulkWithMultipleStreams(
    const IColumn & column,
    OutputStreamGetter getter,
    size_t offset,
    size_t limit,
    bool position_independent_encoding,
    SubstreamPath path) const
{
    path.push_back(Substream::TupleElement);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        path.back().tuple_element = i + 1;
        elems[i]->serializeBinaryBulkWithMultipleStreams(
            extractElementColumn(column, i), getter, offset, limit, position_independent_encoding, path);
    }
}

void DataTypeTuple::deserializeBinaryBulkWithMultipleStreams(
    IColumn & column,
    InputStreamGetter getter,
    size_t limit,
    double avg_value_size_hint,
    bool position_independent_encoding,
    SubstreamPath path) const
{
    path.push_back(Substream::TupleElement);
    for (const auto i : ext::range(0, ext::size(elems)))
    {
        path.back().tuple_element = i + 1;
        elems[i]->deserializeBinaryBulkWithMultipleStreams(
            extractElementColumn(column, i), getter, limit, avg_value_size_hint, position_independent_encoding, path);
    }
}

MutableColumnPtr DataTypeTuple::createColumn() const
{
    size_t size = elems.size();
    Columns tuple_columns(size);
    for (size_t i = 0; i < size; ++i)
        tuple_columns[i] = elems[i]->createColumn();
    return ColumnTuple::create(tuple_columns);
}

Field DataTypeTuple::getDefault() const
{
    return Tuple(ext::map<TupleBackend>(elems, [] (const DataTypePtr & elem) { return elem->getDefault(); }));
}

void DataTypeTuple::insertDefaultInto(IColumn & column) const
{
    addElementSafe(elems, column, [&]
    {
        for (const auto & i : ext::range(0, ext::size(elems)))
            elems[i]->insertDefaultInto(extractElementColumn(column, i));
    });
}


bool DataTypeTuple::textCanContainOnlyValidUTF8() const
{
    return std::all_of(elems.begin(), elems.end(), [](auto && elem) { return elem->textCanContainOnlyValidUTF8(); });
}

bool DataTypeTuple::haveMaximumSizeOfValue() const
{
    return std::all_of(elems.begin(), elems.end(), [](auto && elem) { return elem->haveMaximumSizeOfValue(); });
}

size_t DataTypeTuple::getMaximumSizeOfValueInMemory() const
{
    size_t res = 0;
    for (const auto & elem : elems)
        res += elem->getMaximumSizeOfValueInMemory();
    return res;
}

size_t DataTypeTuple::getSizeOfValueInMemory() const
{
    size_t res = 0;
    for (const auto & elem : elems)
        res += elem->getSizeOfValueInMemory();
    return res;
}


static DataTypePtr create(const ASTPtr & arguments)
{
    if (arguments->children.empty())
        throw Exception("Tuple cannot be empty", ErrorCodes::EMPTY_DATA_PASSED);

    DataTypes nested_types;
    nested_types.reserve(arguments->children.size());

    for (const ASTPtr & child : arguments->children)
        nested_types.emplace_back(DataTypeFactory::instance().get(child));

    return std::make_shared<DataTypeTuple>(nested_types);
}


void registerDataTypeTuple(DataTypeFactory & factory)
{
    factory.registerDataType("Tuple", create);
}

}
