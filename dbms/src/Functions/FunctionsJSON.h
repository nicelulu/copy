#pragma once

#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Common/typeid_cast.h>
#include <Columns/ColumnConst.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnVector.h>
#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnNullable.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnTuple.h>
#include <Core/AccurateComparison.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeEnum.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>
#include <ext/range.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_COLUMN;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}


/// Functions to parse JSONs and extract values from it.
/// The first argument of all these functions gets a JSON,
/// after that there are any number of arguments specifying path to a desired part from the JSON's root.
/// For example,
/// select JSONExtractInt('{"a": "hello", "b": [-100, 200.0, 300]}', b, 1) = -100
template <typename Name, template<typename> typename Impl, typename JSONParser>
class FunctionJSON : public IFunction
{
public:
    static constexpr auto name = Name::name;
    static FunctionPtr create(const Context &) { return std::make_shared<FunctionJSON>(); }
    String getName() const override { return Name::name; }
    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    bool useDefaultImplementationForConstants() const override { return false; }
    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override { return Impl<JSONParser>::getType(arguments); }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result_pos, size_t input_rows_count) override
    {
        MutableColumnPtr to{block.getByPosition(result_pos).type->createColumn()};
        to->reserve(input_rows_count);

        if (arguments.size() < 1)
            throw Exception{"Function " + getName() + " requires at least one arguments", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH};

        const auto & first_column = block.getByPosition(arguments[0]);
        if (!isString(first_column.type))
            throw Exception{"Illegal type " + first_column.type->getName() + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};

        const ColumnPtr & arg_json = first_column.column;
        auto col_json_const = typeid_cast<const ColumnConst *>(arg_json.get());
        auto col_json_string
            = typeid_cast<const ColumnString *>(col_json_const ? col_json_const->getDataColumnPtr().get() : arg_json.get());

        if (!col_json_string)
            throw Exception{"Illegal column " + arg_json->getName() + " of argument of function " + getName(), ErrorCodes::ILLEGAL_COLUMN};

        const ColumnString::Chars & chars = col_json_string->getChars();
        const ColumnString::Offsets & offsets = col_json_string->getOffsets();

        std::vector<Move> moves;
        constexpr size_t num_extra_arguments = Impl<JSONParser>::num_extra_arguments;
        const size_t num_moves = arguments.size() - num_extra_arguments - 1;
        moves.reserve(num_moves);
        for (const auto i : ext::range(0, num_moves))
        {
            const auto & column = block.getByPosition(arguments[1 + i]);
            if (column.column->isColumnConst())
            {
                const auto & column_const = static_cast<const ColumnConst &>(*column.column);
                if (isString(column.type))
                    moves.emplace_back(MoveType::ConstKey, column_const.getField().get<String>());
                else if (isInteger(column.type))
                    moves.emplace_back(MoveType::ConstIndex, column_const.getField().get<Int64>());
                else
                    throw Exception{"Illegal type " + column.type->getName() + " of argument of function " + getName(),
                                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
            }
            else
            {
                if (isString(column.type))
                    moves.emplace_back(MoveType::Key, "");
                else if (isInteger(column.type))
                    moves.emplace_back(MoveType::Index, 0);
                else
                    throw Exception{"Illegal type " + column.type->getName() + " of argument of function " + getName(),
                                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
            }
        }

        JSONParser parser;
        if (parser.need_preallocate)
        {
            size_t max_size = 1;

            for (const auto i : ext::range(0, input_rows_count))
                if (max_size < offsets[i] - offsets[i - 1] - 1)
                    max_size = offsets[i] - offsets[i - 1] - 1;

            parser.preallocate(max_size);
        }

        Impl<JSONParser> impl;
        impl.prepare(block, arguments, result_pos);

        for (const auto i : ext::range(0, input_rows_count))
        {
            bool ok = parser.parse(reinterpret_cast<const char *>(&chars[offsets[i - 1]]), offsets[i] - offsets[i - 1] - 1);

            auto it = parser.getRoot();
            for (const auto j : ext::range(0, moves.size()))
            {
                if (!ok)
                    break;

                switch (moves[j].type)
                {
                    case MoveType::ConstIndex:
                        ok = moveIteratorToElementByIndex(it, moves[j].index);
                        break;
                    case MoveType::ConstKey:
                        ok = moveIteratorToElementByKey(it, moves[j].key);
                        break;
                    case MoveType::Index:
                    {
                        const Field field = (*block.getByPosition(arguments[j + 1]).column)[i];
                        ok = moveIteratorToElementByIndex(it, field.get<Int64>());
                        break;
                    }
                    case MoveType::Key:
                    {
                        const Field field = (*block.getByPosition(arguments[j + 1]).column)[i];
                        ok = moveIteratorToElementByKey(it, field.get<String>().data());
                        break;
                    }
                }
            }

            if (ok)
                ok = impl.addValueToColumn(*to, it);

            if (!ok)
                to->insertDefault();
        }
        block.getByPosition(result_pos).column = std::move(to);
    }

private:
    enum class MoveType
    {
        Key,
        Index,
        ConstKey,
        ConstIndex,
    };
    struct Move
    {
        Move(MoveType type_, size_t index_ = 0) : type(type_), index(index_) {}
        Move(MoveType type_, const String & key_) : type(type_), key(key_) {}
        MoveType type;
        size_t index = 0;
        String key;
    };

    using Iterator = typename JSONParser::Iterator;
    bool moveIteratorToElementByIndex(Iterator & it, int index)
    {
        if (JSONParser::isArray(it))
        {
            if (!JSONParser::downToArray(it))
                return false;
            size_t steps;
            if (index > 0)
            {
                steps = index - 1;
            }
            else
            {
                size_t length = 1;
                Iterator it2 = it;
                while (JSONParser::next(it2))
                    ++length;
                steps = index + length;
            }
            while (steps--)
            {
                if (!JSONParser::next(it))
                    return false;
            }
            return true;
        }
        if (JSONParser::isObject(it))
        {
            if (!JSONParser::downToObject(it))
                return false;
            size_t steps;
            if (index > 0)
            {
                steps = index - 1;
            }
            else
            {
                size_t length = 1;
                Iterator it2 = it;
                while (JSONParser::nextKeyValue(it2))
                    ++length;
                steps = index + length;
            }
            while (steps--)
            {
                if (!JSONParser::nextKeyValue(it))
                    return false;
            }
            return true;
        }
        return false;
    }

    bool moveIteratorToElementByKey(Iterator & it, const String & key)
    {
        if (JSONParser::isObject(it))
        {
            StringRef current_key;
            if (!JSONParser::downToObject(it, current_key))
                return false;
            do
            {
                if (current_key == key)
                    return true;
            } while (JSONParser::nextKeyValue(it, current_key));
        }
        return false;
    }
};


struct NameJSONHas { static constexpr auto name{"JSONHas"}; };
struct NameJSONLength { static constexpr auto name{"JSONLength"}; };
struct NameJSONKey { static constexpr auto name{"JSONKey"}; };
struct NameJSONType { static constexpr auto name{"JSONType"}; };
struct NameJSONExtractInt { static constexpr auto name{"JSONExtractInt"}; };
struct NameJSONExtractUInt { static constexpr auto name{"JSONExtractUInt"}; };
struct NameJSONExtractFloat { static constexpr auto name{"JSONExtractFloat"}; };
struct NameJSONExtractBool { static constexpr auto name{"JSONExtractBool"}; };
struct NameJSONExtractString { static constexpr auto name{"JSONExtractString"}; };
struct NameJSONExtractRaw { static constexpr auto name{"JSONExtractRaw"}; };
struct NameJSONExtract { static constexpr auto name{"JSONExtract"}; };


template <typename JSONParser>
class JSONHasImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &) { return std::make_shared<DataTypeUInt8>(); }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator &)
    {
        ColumnVector<UInt8> & col_vec = static_cast<ColumnVector<UInt8> &>(dest);
        col_vec.insertValue(1);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser>
class JSONLengthImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeUInt64>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        size_t size;
        if (JSONParser::isArray(it))
        {
            size = 0;
            Iterator it2 = it;
            if (JSONParser::downToArray(it2))
            {
                do
                    ++size;
                while (JSONParser::next(it2));
            }
        }
        else if (JSONParser::isObject(it))
        {
            size = 0;
            Iterator it2 = it;
            if (JSONParser::downToObject(it2))
            {
                do
                    ++size;
                while (JSONParser::nextKeyValue(it2));
            }
        }
        else
            return false;

        ColumnVector<UInt64> & col_vec = static_cast<ColumnVector<UInt64> &>(dest);
        col_vec.insertValue(size);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser>
class JSONKeyImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeString>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        if (!JSONParser::parentScopeIsObject(it))
            return false;
        StringRef key = JSONParser::getKey(it);
        ColumnString & col_str = static_cast<ColumnString &>(dest);
        col_str.insertData(key.data, key.size);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser>
class JSONTypeImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        static const std::vector<std::pair<String, Int8>> values = {
            {"Array", '['},
            {"Object", '{'},
            {"String", '"'},
            {"Integer", 'l'},
            {"Float", 'd'},
            {"Bool", 'b'},
            {"Null", 0},
        };
        return std::make_shared<DataTypeEnum<Int8>>(values);
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        UInt8 type;
        if (JSONParser::isInteger(it))
            type = 'l';
        else if (JSONParser::isFloat(it))
            type = 'd';
        else if (JSONParser::isBool(it))
            type = 'b';
        else if (JSONParser::isString(it))
            type = '"';
        else if (JSONParser::isArray(it))
            type = '[';
        else if (JSONParser::isObject(it))
            type = '{';
        else if (JSONParser::isNull(it))
            type = 0;
        else
            return false;

        ColumnVector<Int8> & col_vec = static_cast<ColumnVector<Int8> &>(dest);
        col_vec.insertValue(type);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser, typename NumberType, bool convert_bool_to_integer = false>
class JSONExtractNumericImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeNumber<NumberType>>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        NumberType value;

        if (JSONParser::isInteger(it))
        {
            if (!accurate::convertNumeric(JSONParser::getInteger(it), value))
                return false;
        }
        else if (JSONParser::isFloat(it))
        {
            if (!accurate::convertNumeric(JSONParser::getFloat(it), value))
                return false;
        }
        else if (JSONParser::isBool(it) && std::is_integral_v<NumberType> && convert_bool_to_integer)
            value = static_cast<NumberType>(JSONParser::getBool(it));
        else
            return false;

        auto & col_vec = static_cast<ColumnVector<NumberType> &>(dest);
        col_vec.insertValue(value);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};

template <typename JSONParser>
using JSONExtractInt8Impl = JSONExtractNumericImpl<JSONParser, Int8>;
template <typename JSONParser>
using JSONExtractUInt8Impl = JSONExtractNumericImpl<JSONParser, UInt8>;
template <typename JSONParser>
using JSONExtractInt16Impl = JSONExtractNumericImpl<JSONParser, Int16>;
template <typename JSONParser>
using JSONExtractUInt16Impl = JSONExtractNumericImpl<JSONParser, UInt16>;
template <typename JSONParser>
using JSONExtractInt32Impl = JSONExtractNumericImpl<JSONParser, Int32>;
template <typename JSONParser>
using JSONExtractUInt32Impl = JSONExtractNumericImpl<JSONParser, UInt32>;
template <typename JSONParser>
using JSONExtractInt64Impl = JSONExtractNumericImpl<JSONParser, Int64>;
template <typename JSONParser>
using JSONExtractUInt64Impl = JSONExtractNumericImpl<JSONParser, UInt64>;
template <typename JSONParser>
using JSONExtractFloat32Impl = JSONExtractNumericImpl<JSONParser, Float32>;
template <typename JSONParser>
using JSONExtractFloat64Impl = JSONExtractNumericImpl<JSONParser, Float64>;


template <typename JSONParser>
class JSONExtractBoolImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeUInt8>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        if (!JSONParser::isBool(it))
            return false;

        auto & col_vec = static_cast<ColumnVector<UInt8> &>(dest);
        col_vec.insertValue(static_cast<UInt8>(JSONParser::getBool(it)));
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser>
class JSONExtractStringImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeString>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        if (!JSONParser::isString(it))
            return false;

        StringRef str = JSONParser::getString(it);
        ColumnString & col_str = static_cast<ColumnString &>(dest);
        col_str.insertData(str.data, str.size);
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}
};


template <typename JSONParser>
class JSONExtractRawImpl
{
public:
    static DataTypePtr getType(const ColumnsWithTypeAndName &)
    {
        return std::make_shared<DataTypeString>();
    }

    using Iterator = typename JSONParser::Iterator;
    static bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        ColumnString & col_str = static_cast<ColumnString &>(dest);
        auto & chars = col_str.getChars();
        WriteBufferFromVector<ColumnString::Chars> buf(chars, WriteBufferFromVector<ColumnString::Chars>::AppendModeTag());
        traverse(it, buf);
        buf.finish();
        chars.push_back(0);
        col_str.getOffsets().push_back(chars.size());
        return true;
    }

    static constexpr size_t num_extra_arguments = 0;
    static void prepare(const Block &, const ColumnNumbers &, size_t) {}

private:
    static void traverse(const Iterator & it, WriteBuffer & buf)
    {
        if (JSONParser::isInteger(it))
        {
            writeIntText(JSONParser::getInteger(it), buf);
            return;
        }
        if (JSONParser::isFloat(it))
        {
            writeFloatText(JSONParser::getFloat(it), buf);
            return;
        }
        if (JSONParser::isBool(it))
        {
            if (JSONParser::getBool(it))
                writeCString("true", buf);
            else
                writeCString("false", buf);
            return;
        }
        if (JSONParser::isString(it))
        {
            writeJSONString(JSONParser::getString(it), buf, format_settings());
            return;
        }
        if (JSONParser::isArray(it))
        {
            writeChar('[', buf);
            Iterator it2 = it;
            if (JSONParser::downToArray(it2))
            {
                traverse(it2, buf);
                while (JSONParser::next(it2))
                {
                    writeChar(',', buf);
                    traverse(it2, buf);
                }
            }
            writeChar(']', buf);
            return;
        }
        if (JSONParser::isObject(it))
        {
            writeChar('{', buf);
            Iterator it2 = it;
            StringRef key;
            if (JSONParser::downToObject(it2, key))
            {
                writeJSONString(key, buf, format_settings());
                writeChar(':', buf);
                traverse(it2, buf);
                while (JSONParser::nextKeyValue(it2, key))
                {
                    writeChar(',', buf);
                    writeJSONString(key, buf, format_settings());
                    writeChar(':', buf);
                    traverse(it2, buf);
                }
            }
            writeChar('}', buf);
            return;
        }
        if (JSONParser::isNull(it))
        {
            writeCString("null", buf);
            return;
        }
    }

    static const FormatSettings & format_settings()
    {
        static const FormatSettings the_instance = []
        {
            FormatSettings settings;
            settings.json.escape_forward_slashes = false;
            return settings;
        }();
        return the_instance;
    }
};


template <typename JSONParser>
class JSONExtractImpl
{
public:
    static constexpr size_t num_extra_arguments = 1;

    static DataTypePtr getType(const ColumnsWithTypeAndName & arguments)
    {
        if (arguments.size() < 2)
            throw Exception{"Function JSONExtract requires at least two arguments", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH};

        auto col_type_const = typeid_cast<const ColumnConst *>(arguments.back().column.get());
        if (!col_type_const)
            throw Exception{"Illegal non-const column " + arguments.back().column->getName() + " of the last argument of function JSONExtract",
                            ErrorCodes::ILLEGAL_COLUMN};

        return DataTypeFactory::instance().get(col_type_const->getValue<String>());
    }

    void prepare(const Block & block, const ColumnNumbers &, size_t result_pos)
    {
        extract_tree = buildExtractTree(block.getByPosition(result_pos).type);
    }

    using Iterator = typename JSONParser::Iterator;
    bool addValueToColumn(IColumn & dest, const Iterator & it)
    {
        return extract_tree->addValueToColumn(dest, it);
    }

private:
    class Node
    {
    public:
        Node() {}
        virtual ~Node() {}
        virtual bool addValueToColumn(IColumn &, const Iterator &) = 0;
    };

    template <typename NumberType>
    class NumericNode : public Node
    {
    public:
        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            return JSONExtractNumericImpl<JSONParser, NumberType, true>::addValueToColumn(dest, it);
        }
    };

    class StringNode : public Node
    {
    public:
        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            return JSONExtractStringImpl<JSONParser>::addValueToColumn(dest, it);
        }
    };

    class FixedStringNode : public Node
    {
    public:
        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            if (!JSONParser::isString(it))
                return false;
            auto & col_str = static_cast<ColumnFixedString &>(dest);
            StringRef str = JSONParser::getString(it);
            if (str.size > col_str.getN())
                return false;
            col_str.insertData(str.data, str.size);
            return true;
        }
    };

    template <typename Type>
    class EnumNode : public Node
    {
    public:
        EnumNode(const std::vector<std::pair<String, Type>> & name_value_pairs_) : name_value_pairs(name_value_pairs_)
        {
            for (const auto & name_value_pair : name_value_pairs)
            {
                name_to_value_map.emplace(name_value_pair.first, name_value_pair.second);
                only_values.emplace(name_value_pair.second);
            }
        }

        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            auto & col_vec = static_cast<ColumnVector<Type> &>(dest);

            if (JSONParser::isInteger(it))
            {
                size_t value = static_cast<Type>(JSONParser::getInteger(it));
                if (!only_values.count(value))
                    return false;
                col_vec.insertValue(value);
                return true;
            }

            if (JSONParser::isString(it))
            {
                auto value = name_to_value_map.find(JSONParser::getString(it));
                if (value == name_to_value_map.end())
                    return false;
                col_vec.insertValue(value->second);
                return true;
            }

            return false;
        }

    private:
        std::vector<std::pair<String, Type>> name_value_pairs;
        std::unordered_map<StringRef, Type> name_to_value_map;
        std::unordered_set<Type> only_values;
    };

    class NullableNode : public Node
    {
    public:
        NullableNode(std::unique_ptr<Node> nested_) : nested(std::move(nested_)) {}

        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            ColumnNullable & col_null = static_cast<ColumnNullable &>(dest);
            if (!nested->addValueToColumn(col_null.getNestedColumn(), it))
                return false;
            col_null.getNullMapColumn().insertValue(0);
            return true;
        }

    private:
        std::unique_ptr<Node> nested;
    };

    class ArrayNode : public Node
    {
    public:
        ArrayNode(std::unique_ptr<Node> nested_) : nested(std::move(nested_)) {}

        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            if (!JSONParser::isArray(it))
                return false;

            Iterator it2 = it;
            if (!JSONParser::downToArray(it2))
                return false;

            ColumnArray & col_arr = static_cast<ColumnArray &>(dest);
            auto & data = col_arr.getData();
            size_t old_size = data.size();
            bool were_valid_elements = false;

            do
            {
                if (nested->addValueToColumn(data, it2))
                    were_valid_elements = true;
                else
                    data.insertDefault();
            }
            while (JSONParser::next(it2));

            if (!were_valid_elements)
            {
                data.popBack(data.size() - old_size);
                return false;
            }

            col_arr.getOffsets().push_back(data.size());
            return true;
        }

    private:
        std::unique_ptr<Node> nested;
    };

    class TupleNode : public Node
    {
    public:
        TupleNode(std::vector<std::unique_ptr<Node>> nested_, const std::vector<String> & explicit_names_) : nested(std::move(nested_)), explicit_names(explicit_names_)
        {
            for (size_t i = 0; i != explicit_names.size(); ++i)
                name_to_index_map.emplace(explicit_names[i], i);
        }

        bool addValueToColumn(IColumn & dest, const Iterator & it) override
        {
            ColumnTuple & tuple = static_cast<ColumnTuple &>(dest);
            size_t old_size = dest.size();
            bool were_valid_elements = false;

            auto set_size = [&](size_t size)
            {
                for (size_t i = 0; i != tuple.tupleSize(); ++i)
                {
                    auto & col = tuple.getColumn(i);
                    if (col.size() != size)
                    {
                        if (col.size() > size)
                            col.popBack(col.size() - size);
                        else
                            while (col.size() < size)
                                col.insertDefault();
                    }
                }
            };

            if (JSONParser::isArray(it))
            {
                Iterator it2 = it;
                if (!JSONParser::downToArray(it2))
                    return false;

                size_t index = 0;
                do
                {
                    if (nested[index]->addValueToColumn(tuple.getColumn(index), it2))
                        were_valid_elements = true;
                    else
                        tuple.getColumn(index).insertDefault();
                    ++index;
                }
                while (JSONParser::next(it2));

                set_size(old_size + static_cast<size_t>(were_valid_elements));
                return were_valid_elements;
            }

            if (JSONParser::isObject(it))
            {
                if (name_to_index_map.empty())
                {
                    Iterator it2 = it;
                    if (!JSONParser::downToObject(it2))
                        return false;

                    size_t index = 0;
                    do
                    {
                        if (nested[index]->addValueToColumn(tuple.getColumn(index), it2))
                            were_valid_elements = true;
                        else
                            tuple.getColumn(index).insertDefault();
                        ++index;
                    }
                    while (JSONParser::nextKeyValue(it2));
                }
                else
                {
                    Iterator it2 = it;
                    StringRef key;
                    if (!JSONParser::downToObject(it2, key))
                        return false;

                    do
                    {
                        auto index = name_to_index_map.find(key);
                        if (index != name_to_index_map.end())
                        {
                            if (nested[index->second]->addValueToColumn(tuple.getColumn(index->second), it2))
                                were_valid_elements = true;
                        }
                    }
                    while (JSONParser::nextKeyValue(it2, key));
                }

                set_size(old_size + static_cast<size_t>(were_valid_elements));
                return were_valid_elements;
            }

            return false;
        }

    private:
        std::vector<std::unique_ptr<Node>> nested;
        std::vector<String> explicit_names;
        std::unordered_map<StringRef, size_t> name_to_index_map;
    };

    std::unique_ptr<Node> buildExtractTree(const DataTypePtr & type)
    {
        switch (type->getTypeId())
        {
            case TypeIndex::UInt8: return std::make_unique<NumericNode<UInt8>>();
            case TypeIndex::UInt16: return std::make_unique<NumericNode<UInt16>>();
            case TypeIndex::UInt32: return std::make_unique<NumericNode<UInt32>>();
            case TypeIndex::UInt64: return std::make_unique<NumericNode<UInt64>>();
            case TypeIndex::Int8: return std::make_unique<NumericNode<Int8>>();
            case TypeIndex::Int16: return std::make_unique<NumericNode<Int16>>();
            case TypeIndex::Int32: return std::make_unique<NumericNode<Int32>>();
            case TypeIndex::Int64: return std::make_unique<NumericNode<Int64>>();
            case TypeIndex::Float32: return std::make_unique<NumericNode<Float32>>();
            case TypeIndex::Float64: return std::make_unique<NumericNode<Float64>>();
            case TypeIndex::String: return std::make_unique<StringNode>();
            case TypeIndex::FixedString: return std::make_unique<FixedStringNode>();
            case TypeIndex::Enum8: return std::make_unique<EnumNode<Int8>>(static_cast<const DataTypeEnum8 &>(*type).getValues());
            case TypeIndex::Enum16: return std::make_unique<EnumNode<Int16>>(static_cast<const DataTypeEnum16 &>(*type).getValues());
            case TypeIndex::Nullable: return std::make_unique<NullableNode>(buildExtractTree(static_cast<const DataTypeNullable &>(*type).getNestedType()));
            case TypeIndex::Array: return std::make_unique<ArrayNode>(buildExtractTree(static_cast<const DataTypeArray &>(*type).getNestedType()));
            case TypeIndex::Tuple:
            {
                const auto & tuple = static_cast<const DataTypeTuple &>(*type);
                const auto & tuple_elements = tuple.getElements();
                std::vector<std::unique_ptr<Node>> elements;
                for (const auto & tuple_element : tuple_elements)
                    elements.emplace_back(buildExtractTree(tuple_element));
                return std::make_unique<TupleNode>(std::move(elements), tuple.haveExplicitNames() ? tuple.getElementNames() : Strings{});
            }
            default:
                throw Exception{"Unsupported return type schema: " + type->getName(), ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
        }
    }

    std::unique_ptr<Node> extract_tree;
};


template <typename JSONParser>
void registerFunctionsJSONTemplate(FunctionFactory & factory)
{
    factory.registerFunction<FunctionJSON<NameJSONHas, JSONHasImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONLength, JSONLengthImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONKey, JSONKeyImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONType, JSONTypeImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractInt, JSONExtractInt64Impl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractUInt, JSONExtractUInt64Impl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractFloat, JSONExtractFloat64Impl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractBool, JSONExtractBoolImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractString, JSONExtractStringImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtractRaw, JSONExtractRawImpl, JSONParser>>();
    factory.registerFunction<FunctionJSON<NameJSONExtract, JSONExtractImpl, JSONParser>>();
}

}
