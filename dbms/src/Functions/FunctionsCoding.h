#pragma once

#include <Common/hex.h>
#include <Common/formatIPv6.h>
#include <Common/typeid_cast.h>
#include <IO/WriteHelpers.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeFixedString.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeDate.h>
#include <DataTypes/DataTypeDateTime.h>
#include <Columns/ColumnsNumber.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnConst.h>
#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>

#include <arpa/inet.h>

#include <ext/range.h>
#include <array>


namespace DB
{

namespace ErrorCodes
{
    extern const int TOO_LESS_ARGUMENTS_FOR_FUNCTION;
}


/** TODO This file contains ridiculous amount of copy-paste.
  */

/** Encoding functions:
  *
  * IPv4NumToString (num) - See below.
  * IPv4StringToNum(string) - Convert, for example, '192.168.0.1' to 3232235521 and vice versa.
  *
  * hex(x) - Returns hex; capital letters; there are no prefixes 0x or suffixes h.
  *          For numbers, returns a variable-length string - hex in the "human" (big endian) format, with the leading zeros being cut,
  *          but only by whole bytes. For dates and datetimes - the same as for numbers.
  *          For example, hex(257) = '0101'.
  * unhex(string) - Returns a string, hex of which is equal to `string` with regard of case and discarding one leading zero.
  *                 If such a string does not exist, could return arbitary implementation specific value.
  *
  * bitmaskToArray(x) - Returns an array of powers of two in the binary form of x. For example, bitmaskToArray(50) = [2, 16, 32].
  */


constexpr auto ipv4_bytes_length = 4;
constexpr auto ipv6_bytes_length = 16;
constexpr auto uuid_bytes_length = 16;
constexpr auto uuid_text_length = 36;


class FunctionIPv6NumToString : public IFunction
{
public:
    static constexpr auto name = "IPv6NumToString";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv6NumToString>(); }

    String getName() const override { return name; }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        const auto ptr = checkAndGetDataType<DataTypeFixedString>(arguments[0].get());
        if (!ptr || ptr->getN() != ipv6_bytes_length)
            throw Exception("Illegal type " + arguments[0]->getName() +
                            " of argument of function " + getName() +
                            ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, const size_t result) override
    {
        const auto & col_type_name = block.getByPosition(arguments[0]);
        const ColumnPtr & column = col_type_name.column;

        if (const auto col_in = checkAndGetColumn<ColumnFixedString>(column.get()))
        {
            if (col_in->getN() != ipv6_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto size = col_in->size();
            const auto & vec_in = col_in->getChars();

            auto col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();
            vec_res.resize(size * (IPV6_MAX_TEXT_LENGTH + 1));
            offsets_res.resize(size);

            auto begin = reinterpret_cast<char *>(&vec_res[0]);
            auto pos = begin;

            for (size_t offset = 0, i = 0; offset < vec_in.size(); offset += ipv6_bytes_length, ++i)
            {
                formatIPv6(&vec_in[offset], pos);
                offsets_res[i] = pos - begin;
            }

            vec_res.resize(pos - begin);
        }
        else if (const auto col_in = checkAndGetColumnConst<ColumnFixedString>(column.get()))
        {
            const auto column_fixed_string = checkAndGetColumn<ColumnFixedString>(&col_in->getDataColumn());
            if (!column_fixed_string || column_fixed_string->getN() != ipv6_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            String data_in = col_in->getValue<String>();

            char buf[IPV6_MAX_TEXT_LENGTH + 1];
            char * dst = buf;
            formatIPv6(reinterpret_cast<const unsigned char *>(data_in.data()), dst);

            block.getByPosition(result).column = DataTypeString().createConstColumn(col_in->size(), String(buf));
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionCutIPv6 : public IFunction
{
public:
    static constexpr auto name = "cutIPv6";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionCutIPv6>(); }

    String getName() const override { return name; }

    size_t getNumberOfArguments() const override { return 3; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        const auto ptr = checkAndGetDataType<DataTypeFixedString>(arguments[0].get());
        if (!ptr || ptr->getN() != ipv6_bytes_length)
            throw Exception("Illegal type " + arguments[0]->getName() +
                            " of argument 1 of function " + getName() +
                            ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (!checkDataType<DataTypeUInt8>(arguments[1].get()))
            throw Exception("Illegal type " + arguments[1]->getName() +
                            " of argument 2 of function " + getName(),
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (!checkDataType<DataTypeUInt8>(arguments[2].get()))
            throw Exception("Illegal type " + arguments[2]->getName() +
                            " of argument 3 of function " + getName(),
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, const size_t result) override
    {
        const auto & col_type_name = block.getByPosition(arguments[0]);
        const ColumnPtr & column = col_type_name.column;

        const auto & col_ipv6_zeroed_tail_bytes_type = block.getByPosition(arguments[1]);
        const auto & col_ipv6_zeroed_tail_bytes = col_ipv6_zeroed_tail_bytes_type.column;
        const auto & col_ipv4_zeroed_tail_bytes_type = block.getByPosition(arguments[2]);
        const auto & col_ipv4_zeroed_tail_bytes = col_ipv4_zeroed_tail_bytes_type.column;

        if (const auto col_in = checkAndGetColumn<ColumnFixedString>(column.get()))
        {
            if (col_in->getN() != ipv6_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto ipv6_zeroed_tail_bytes = checkAndGetColumnConst<ColumnVector<UInt8>>(col_ipv6_zeroed_tail_bytes.get());
            if (!ipv6_zeroed_tail_bytes)
                throw Exception("Illegal type " + col_ipv6_zeroed_tail_bytes_type.type->getName() +
                                " of argument 2 of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            UInt8 ipv6_zeroed_tail_bytes_count = ipv6_zeroed_tail_bytes->getValue<UInt8>();
            if (ipv6_zeroed_tail_bytes_count > ipv6_bytes_length)
                throw Exception("Illegal value for argument 2 " + col_ipv6_zeroed_tail_bytes_type.type->getName() +
                                " of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto ipv4_zeroed_tail_bytes = checkAndGetColumnConst<ColumnVector<UInt8>>(col_ipv4_zeroed_tail_bytes.get());
            if (!ipv4_zeroed_tail_bytes)
                throw Exception("Illegal type " + col_ipv4_zeroed_tail_bytes_type.type->getName() +
                                " of argument 3 of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            UInt8 ipv4_zeroed_tail_bytes_count = ipv4_zeroed_tail_bytes->getValue<UInt8>();
            if (ipv4_zeroed_tail_bytes_count > ipv6_bytes_length)
                throw Exception("Illegal value for argument 3 " + col_ipv4_zeroed_tail_bytes_type.type->getName() +
                                " of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto size = col_in->size();
            const auto & vec_in = col_in->getChars();

            auto col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();
            vec_res.resize(size * (IPV6_MAX_TEXT_LENGTH + 1));
            offsets_res.resize(size);

            auto begin = reinterpret_cast<char *>(&vec_res[0]);
            auto pos = begin;

            for (size_t offset = 0, i = 0; offset < vec_in.size(); offset += ipv6_bytes_length, ++i)
            {
                const auto address = &vec_in[offset];
                UInt8 zeroed_tail_bytes_count = isIPv4Mapped(address) ? ipv4_zeroed_tail_bytes_count : ipv6_zeroed_tail_bytes_count;
                cutAddress(address, pos, zeroed_tail_bytes_count);
                offsets_res[i] = pos - begin;
            }

            vec_res.resize(pos - begin);
        }
        else if (const auto col_in = checkAndGetColumnConst<ColumnFixedString>(column.get()))
        {
            const auto column_fixed_string = checkAndGetColumn<ColumnFixedString>(&col_in->getDataColumn());
            if (!column_fixed_string || column_fixed_string->getN() != ipv6_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto ipv6_zeroed_tail_bytes = checkAndGetColumnConst<ColumnVector<UInt8>>(col_ipv6_zeroed_tail_bytes.get());
            if (!ipv6_zeroed_tail_bytes)
                throw Exception("Illegal type " + col_ipv6_zeroed_tail_bytes_type.type->getName() +
                                " of argument 2 of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            UInt8 ipv6_zeroed_tail_bytes_count = ipv6_zeroed_tail_bytes->getValue<UInt8>();
            if (ipv6_zeroed_tail_bytes_count > ipv6_bytes_length)
                throw Exception("Illegal value for argument 2 " + col_ipv6_zeroed_tail_bytes_type.type->getName() +
                                " of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto ipv4_zeroed_tail_bytes = checkAndGetColumnConst<ColumnVector<UInt8>>(col_ipv4_zeroed_tail_bytes.get());
            if (!ipv4_zeroed_tail_bytes)
                throw Exception("Illegal type " + col_ipv4_zeroed_tail_bytes_type.type->getName() +
                                " of argument 3 of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            UInt8 ipv4_zeroed_tail_bytes_count = ipv4_zeroed_tail_bytes->getValue<UInt8>();
            if (ipv4_zeroed_tail_bytes_count > ipv6_bytes_length)
                throw Exception("Illegal value for argument 3 " + col_ipv6_zeroed_tail_bytes_type.type->getName() +
                                " of function " + getName(),
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            String data_in = col_in->getValue<String>();

            char buf[IPV6_MAX_TEXT_LENGTH + 1];
            char * dst = buf;

            const auto address = reinterpret_cast<const unsigned char *>(data_in.data());
            UInt8 zeroed_tail_bytes_count = isIPv4Mapped(address) ? ipv4_zeroed_tail_bytes_count : ipv6_zeroed_tail_bytes_count;
            cutAddress(address, dst, zeroed_tail_bytes_count);

            block.getByPosition(result).column = DataTypeString().createConstColumn(col_in->size(), String(buf));
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }

private:
    bool isIPv4Mapped(const unsigned char * address) const
    {
        return (*reinterpret_cast<const UInt64 *>(&address[0]) == 0) &&
            ((*reinterpret_cast<const UInt64 *>(&address[8]) & 0x00000000FFFFFFFFull) == 0x00000000FFFF0000ull);
    }

    void cutAddress(const unsigned char * address, char *& dst, UInt8 zeroed_tail_bytes_count)
    {
        formatIPv6(address, dst, zeroed_tail_bytes_count);
    }
};


class FunctionIPv6StringToNum : public IFunction
{
public:
    static constexpr auto name = "IPv6StringToNum";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv6StringToNum>(); }

    String getName() const override { return name; }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeFixedString>(ipv6_bytes_length);
    }


    static bool ipv4_scan(const char * src, unsigned char * dst)
    {
        constexpr auto size = sizeof(UInt32);
        char bytes[size]{};

        for (const auto i : ext::range(0, size))
        {
            UInt32 value = 0;
            size_t len = 0;
            while (isNumericASCII(*src) && len <= 3)
            {
                value = value * 10 + (*src - '0');
                ++len;
                ++src;
            }

            if (len == 0 || value > 255 || (i < size - 1 && *src != '.'))
            {
                memset(dst, 0, size);
                return false;
            }
            bytes[i] = value;
            ++src;
        }

        if (src[-1] != '\0')
        {
            memset(dst, 0, size);
            return false;
        }

        memcpy(dst, bytes, sizeof(bytes));
        return true;
    }

    /// slightly altered implementation from http://svn.apache.org/repos/asf/apr/apr/trunk/network_io/unix/inet_pton.c
    static void ipv6_scan(const char *  src, unsigned char * dst)
    {
        const auto clear_dst = [dst]
        {
            memset(dst, '\0', ipv6_bytes_length);
        };

        /// Leading :: requires some special handling.
        if (*src == ':')
            if (*++src != ':')
                return clear_dst();

        /// get integer value for a hexademical char digit, or -1
        const auto number_by_char = [] (const char ch)
        {
            if ('A' <= ch && ch <= 'F')
                return 10 + ch - 'A';

            if ('a' <= ch && ch <= 'f')
                return 10 + ch - 'a';

            if ('0' <= ch && ch <= '9')
                return ch - '0';

            return -1;
        };

        unsigned char tmp[ipv6_bytes_length]{};
        auto tp = tmp;
        auto endp = tp + ipv6_bytes_length;
        auto curtok = src;
        auto saw_xdigit = false;
        uint16_t val{};
        unsigned char * colonp = nullptr;

        while (const auto ch = *src++)
        {
            const auto num = number_by_char(ch);

            if (num != -1)
            {
                val <<= 4;
                val |= num;
                if (val > 0xffffu)
                    return clear_dst();

                saw_xdigit = 1;
                continue;
            }

            if (ch == ':')
            {
                curtok = src;
                if (!saw_xdigit)
                {
                    if (colonp)
                        return clear_dst();

                    colonp = tp;
                    continue;
                }

                if (tp + sizeof(uint16_t) > endp)
                    return clear_dst();

                *tp++ = static_cast<unsigned char>((val >> 8) & 0xffu);
                *tp++ = static_cast<unsigned char>(val & 0xffu);
                saw_xdigit = false;
                val = 0;
                continue;
            }

            if (ch == '.' && (tp + ipv4_bytes_length) <= endp)
            {
                if (!ipv4_scan(curtok, tp))
                    return clear_dst();

                tp += ipv4_bytes_length;
                saw_xdigit = false;
                break;    /* '\0' was seen by ipv4_scan(). */
            }

            return clear_dst();
        }

        if (saw_xdigit)
        {
            if (tp + sizeof(uint16_t) > endp)
                return clear_dst();

            *tp++ = static_cast<unsigned char>((val >> 8) & 0xffu);
            *tp++ = static_cast<unsigned char>(val & 0xffu);
        }

        if (colonp)
        {
            /*
             * Since some memmove()'s erroneously fail to handle
             * overlapping regions, we'll do the shift by hand.
             */
            const auto n = tp - colonp;

            for (int i = 1; i <= n; i++)
            {
                endp[- i] = colonp[n - i];
                colonp[n - i] = 0;
            }
            tp = endp;
        }

        if (tp != endp)
            return clear_dst();

        memcpy(dst, tmp, sizeof(tmp));
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const auto col_in = checkAndGetColumn<ColumnString>(column.get()))
        {
            const auto col_res = std::make_shared<ColumnFixedString>(ipv6_bytes_length);
            block.getByPosition(result).column = col_res;

            auto & vec_res = col_res->getChars();
            vec_res.resize(col_in->size() * ipv6_bytes_length);

            const ColumnString::Chars_t & vec_src = col_in->getChars();
            const ColumnString::Offsets_t & offsets_src = col_in->getOffsets();
            size_t src_offset = 0;

            for (size_t out_offset = 0, i = 0;
                 out_offset < vec_res.size();
                 out_offset += ipv6_bytes_length, ++i)
            {
                ipv6_scan(reinterpret_cast<const char * >(&vec_src[src_offset]), &vec_res[out_offset]);
                src_offset = offsets_src[i];
            }
        }
        else if (const auto col_in = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            String out(ipv6_bytes_length, 0);
            ipv6_scan(col_in->getValue<String>().data(), reinterpret_cast<unsigned char *>(&out[0]));

            block.getByPosition(result).column = DataTypeFixedString(ipv6_bytes_length).createConstColumn(col_in->size(), out);
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionIPv4NumToString : public IFunction
{
public:
    static constexpr auto name = "IPv4NumToString";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv4NumToString>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeUInt32>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName() + ", expected UInt32",
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    static void formatIP(UInt32 ip, char *& out)
    {
        char * begin = out;

        /// Write everything backwards.
        for (size_t offset = 0; offset <= 24; offset += 8)
        {
            if (offset > 0)
                *(out++) = '.';

            /// Get the next byte.
            UInt32 value = (ip >> offset) & static_cast<UInt32>(255);

            /// Faster than sprintf.
            if (value == 0)
            {
                *(out++) = '0';
            }
            else
            {
                while (value > 0)
                {
                    *(out++) = '0' + value % 10;
                    value /= 10;
                }
            }
        }

        /// And reverse.
        std::reverse(begin, out);

        *(out++) = '\0';
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnUInt32 * col = typeid_cast<const ColumnUInt32 *>(column.get()))
        {
            const ColumnUInt32::Container_t & vec_in = col->getData();

            std::shared_ptr<ColumnString> col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();

            vec_res.resize(vec_in.size() * (IPV4_MAX_TEXT_LENGTH + 1)); /// the longest value is: 255.255.255.255\0
            offsets_res.resize(vec_in.size());
            char * begin = reinterpret_cast<char *>(&vec_res[0]);
            char * pos = begin;

            for (size_t i = 0; i < vec_in.size(); ++i)
            {
                formatIP(vec_in[i], pos);
                offsets_res[i] = pos - begin;
            }

            vec_res.resize(pos - begin);
        }
        else if (auto col = checkAndGetColumnConst<ColumnUInt32>(column.get()))
        {
            char buf[16];
            char * pos = buf;
            formatIP(col->getValue<UInt32>(), pos);

            auto col_res = DataTypeString().createConstColumn(col->size(), String(buf));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionIPv4StringToNum : public IFunction
{
public:
    static constexpr auto name = "IPv4StringToNum";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv4StringToNum>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeUInt32>();
    }

    static UInt32 parseIPv4(const char * pos)
    {
        UInt32 res = 0;
        for (int offset = 24; offset >= 0; offset -= 8)
        {
            UInt32 value = 0;
            size_t len = 0;
            while (isNumericASCII(*pos) && len <= 3)
            {
                value = value * 10 + (*pos - '0');
                ++len;
                ++pos;
            }
            if (len == 0 || value > 255 || (offset > 0 && *pos != '.'))
                return 0;
            res |= value << offset;
            ++pos;
        }
        if (*(pos - 1) != '\0')
            return 0;
        return res;
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnString * col = checkAndGetColumn<ColumnString>(column.get()))
        {
            auto col_res = std::make_shared<ColumnUInt32>();
            block.getByPosition(result).column = col_res;

            ColumnUInt32::Container_t & vec_res = col_res->getData();
            vec_res.resize(col->size());

            const ColumnString::Chars_t & vec_src = col->getChars();
            const ColumnString::Offsets_t & offsets_src = col->getOffsets();
            size_t prev_offset = 0;

            for (size_t i = 0; i < vec_res.size(); ++i)
            {
                vec_res[i] = parseIPv4(reinterpret_cast<const char *>(&vec_src[prev_offset]));
                prev_offset = offsets_src[i];
            }
        }
        else if (const ColumnConst * col = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            auto col_res = DataTypeUInt32().createConstColumn(col->size(), toField(parseIPv4(col->getValue<String>().c_str())));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionIPv4NumToStringClassC : public IFunction
{
public:
    static constexpr auto name = "IPv4NumToStringClassC";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv4NumToStringClassC>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeUInt32>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName() + ", expected UInt32",
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    static void formatIP(UInt32 ip, char *& out)
    {
        char * begin = out;

        for (auto i = 0; i < 3; ++i)
            *(out++) = 'x';

        /// Write everything backwards.
        for (size_t offset = 8; offset <= 24; offset += 8)
        {
            if (offset > 0)
                *(out++) = '.';

            /// Get the next byte.
            UInt32 value = (ip >> offset) & static_cast<UInt32>(255);

            /// Faster than sprintf.
            if (value == 0)
            {
                *(out++) = '0';
            }
            else
            {
                while (value > 0)
                {
                    *(out++) = '0' + value % 10;
                    value /= 10;
                }
            }
        }

        /// And reverse.
        std::reverse(begin, out);

        *(out++) = '\0';
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnUInt32 * col = typeid_cast<const ColumnUInt32 *>(column.get()))
        {
            const ColumnUInt32::Container_t & vec_in = col->getData();

            std::shared_ptr<ColumnString> col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();

            vec_res.resize(vec_in.size() * (IPV4_MAX_TEXT_LENGTH + 1)); /// the longest value is: 255.255.255.255\0
            offsets_res.resize(vec_in.size());
            char * begin = reinterpret_cast<char *>(&vec_res[0]);
            char * pos = begin;

            for (size_t i = 0; i < vec_in.size(); ++i)
            {
                formatIP(vec_in[i], pos);
                offsets_res[i] = pos - begin;
            }

            vec_res.resize(pos - begin);
        }
        else if (auto col = checkAndGetColumnConst<ColumnUInt32>(column.get()))
        {
            char buf[16];
            char * pos = buf;
            formatIP(col->getValue<UInt32>(), pos);

            auto col_res = DataTypeString().createConstColumn(col->size(), String(buf));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionIPv4ToIPv6 : public IFunction
{
public:
     static constexpr auto name = "IPv4ToIPv6";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionIPv4ToIPv6>(); }

    String getName() const override { return name; }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkAndGetDataType<DataTypeUInt32>(arguments[0].get()))
            throw Exception("Illegal type " + arguments[0]->getName() +
                            " of argument of function " + getName(), ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeFixedString>(16);
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, const size_t result) override
    {
        const auto & col_type_name = block.getByPosition(arguments[0]);
        const ColumnPtr & column = col_type_name.column;

        if (const auto col_in = typeid_cast<const ColumnUInt32 *>(column.get()))
        {
            const auto col_res = std::make_shared<ColumnFixedString>(ipv6_bytes_length);
            block.getByPosition(result).column = col_res;

            auto & vec_res = col_res->getChars();
            vec_res.resize(col_in->size() * ipv6_bytes_length);

            const auto & vec_in = col_in->getData();

            for (size_t out_offset = 0, i = 0; out_offset < vec_res.size(); out_offset += ipv6_bytes_length, ++i)
                mapIPv4ToIPv6(vec_in[i], &vec_res[out_offset]);
        }
        else if (const auto col_in = checkAndGetColumnConst<ColumnVector<UInt32>>(column.get()))
        {
            std::string buf;
            buf.resize(ipv6_bytes_length);
            mapIPv4ToIPv6(col_in->getValue<UInt32>(), reinterpret_cast<unsigned char *>(&buf[0]));
            block.getByPosition(result).column = DataTypeFixedString(ipv6_bytes_length).createConstColumn(ipv6_bytes_length, buf);
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }

private:
    void mapIPv4ToIPv6(UInt32 in, unsigned char * buf) const
    {
        *reinterpret_cast<UInt64 *>(&buf[0]) = 0;
        *reinterpret_cast<UInt64 *>(&buf[8]) = 0x00000000FFFF0000ull | (static_cast<UInt64>(ntohl(in)) << 32);
    }
};


class FunctionMACNumToString : public IFunction
{
public:
    static constexpr auto name = "MACNumToString";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionMACNumToString>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeUInt64>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName() + ", expected UInt64",
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    static void formatMAC(UInt64 mac, char *& out)
    {
        char * begin = out;

        /// Write everything backwards.
        for (size_t offset = 0; offset <= 40; offset += 8)
        {
            if (offset > 0)
                *(out++) = ':';

            /// Get the next byte.
            UInt64 value = (mac >> offset) & static_cast<UInt64>(255);

            /// Faster than sprintf.
            if (value < 16)
            {
                *(out++) = '0';
            }
            if (value == 0)
            {
                *(out++) = '0';
            }
            else
            {
                while (value > 0)
                {
                    *(out++) = hexUppercase(value % 16);
                    value /= 16;
                }
            }
        }

        /// And reverse.
        std::reverse(begin, out);

        *(out++) = '\0';
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnUInt64 * col = typeid_cast<const ColumnUInt64 *>(column.get()))
        {
            const ColumnUInt64::Container_t & vec_in = col->getData();

            std::shared_ptr<ColumnString> col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();

            vec_res.resize(vec_in.size() * 18); /// the longest value is: xx:xx:xx:xx:xx:xx\0
            offsets_res.resize(vec_in.size());
            char * begin = reinterpret_cast<char *>(&vec_res[0]);
            char * pos = begin;

            for (size_t i = 0; i < vec_in.size(); ++i)
            {
                formatMAC(vec_in[i], pos);
                offsets_res[i] = pos - begin;
            }

            vec_res.resize(pos - begin);
        }
        else if (auto col = checkAndGetColumnConst<ColumnUInt64>(column.get()))
        {
            char buf[18];
            char * pos = buf;
            formatMAC(col->getValue<UInt64>(), pos);

            auto col_res = DataTypeString().createConstColumn(col->size(), String(buf));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionMACStringToNum : public IFunction
{
public:
    static constexpr auto name = "MACStringToNum";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionMACStringToNum>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeUInt64>();
    }

    static UInt64 parseMAC(const char * pos)
    {

        /// get integer value for a hexademical char digit, or -1
        const auto number_by_char = [] (const char ch)
        {
            if ('A' <= ch && ch <= 'F')
                return 10 + ch - 'A';

            if ('a' <= ch && ch <= 'f')
                return 10 + ch - 'a';

            if ('0' <= ch && ch <= '9')
                return ch - '0';

            return -1;
        };

        UInt64 res = 0;
        for (int offset = 40; offset >= 0; offset -= 8)
        {
            UInt64 value = 0;
            size_t len = 0;
            int val = 0;
            while ((val = number_by_char(*pos)) >= 0 && len <= 2)
            {
                value = value * 16 + val;
                ++len;
                ++pos;
            }
            if (len == 0 || value > 255 || (offset > 0 && *pos != ':'))
                return 0;
            res |= value << offset;
            ++pos;
        }
        if (*(pos - 1) != '\0')
            return 0;
        return res;
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnString * col = checkAndGetColumn<ColumnString>(column.get()))
        {
            auto col_res = std::make_shared<ColumnUInt64>();
            block.getByPosition(result).column = col_res;

            ColumnUInt64::Container_t & vec_res = col_res->getData();
            vec_res.resize(col->size());

            const ColumnString::Chars_t & vec_src = col->getChars();
            const ColumnString::Offsets_t & offsets_src = col->getOffsets();
            size_t prev_offset = 0;

            for (size_t i = 0; i < vec_res.size(); ++i)
            {
                vec_res[i] = parseMAC(reinterpret_cast<const char *>(&vec_src[prev_offset]));
                prev_offset = offsets_src[i];
            }
        }
        else if (const ColumnConst * col = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            auto col_res = DataTypeUInt64().createConstColumn(col->size(), parseMAC(col->getValue<String>().c_str()));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_COLUMN);
    }
};

class FunctionMACStringToOUI : public IFunction
{
public:
    static constexpr auto name = "MACStringToOUI";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionMACStringToOUI>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeUInt64>();
    }

    static UInt64 parseMAC(const char * pos)
    {

        /// get integer value for a hexademical char digit, or -1
        const auto number_by_char = [] (const char ch)
        {
            if ('A' <= ch && ch <= 'F')
                return 10 + ch - 'A';

            if ('a' <= ch && ch <= 'f')
                return 10 + ch - 'a';

            if ('0' <= ch && ch <= '9')
                return ch - '0';

            return -1;
        };

        UInt64 res = 0;
        for (int offset = 40; offset >= 0; offset -= 8)
        {
            UInt64 value = 0;
            size_t len = 0;
            int val = 0;
            while ((val = number_by_char(*pos)) >= 0 && len <= 2)
            {
                value = value * 16 + val;
                ++len;
                ++pos;
            }
            if (len == 0 || value > 255 || (offset > 0 && *pos != ':'))
                return 0;
            res |= value << offset;
            ++pos;
        }
        if (*(pos - 1) != '\0')
            return 0;
        res = res >> 24;
        return res;
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnString * col = checkAndGetColumn<ColumnString>(column.get()))
        {
            auto col_res = std::make_shared<ColumnUInt64>();
            block.getByPosition(result).column = col_res;

            ColumnUInt64::Container_t & vec_res = col_res->getData();
            vec_res.resize(col->size());

            const ColumnString::Chars_t & vec_src = col->getChars();
            const ColumnString::Offsets_t & offsets_src = col->getOffsets();
            size_t prev_offset = 0;

            for (size_t i = 0; i < vec_res.size(); ++i)
            {
                vec_res[i] = parseMAC(reinterpret_cast<const char *>(&vec_src[prev_offset]));
                prev_offset = offsets_src[i];
            }
        }
        else if (const ColumnConst * col = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            auto col_res = DataTypeUInt64().createConstColumn(col->size(), parseMAC(col->getValue<String>().c_str()));
            block.getByPosition(result).column = col_res;
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionUUIDNumToString : public IFunction
{

public:
    static constexpr auto name = "UUIDNumToString";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionUUIDNumToString>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        const auto ptr = checkAndGetDataType<DataTypeFixedString>(arguments[0].get());
        if (!ptr || ptr->getN() != uuid_bytes_length)
            throw Exception("Illegal type " + arguments[0]->getName() +
                            " of argument of function " + getName() +
                            ", expected FixedString(" + toString(uuid_bytes_length) + ")",
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnWithTypeAndName & col_type_name = block.getByPosition(arguments[0]);
        const ColumnPtr & column = col_type_name.column;

        if (const auto col_in = checkAndGetColumn<ColumnFixedString>(column.get()))
        {
            if (col_in->getN() != uuid_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(uuid_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto size = col_in->size();
            const auto & vec_in = col_in->getChars();

            auto col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            ColumnString::Offsets_t & offsets_res = col_res->getOffsets();
            vec_res.resize(size * (uuid_text_length + 1));
            offsets_res.resize(size);

            size_t src_offset = 0;
            size_t dst_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                formatUUID(&vec_in[src_offset], &vec_res[dst_offset]);
                src_offset += uuid_bytes_length;
                dst_offset += uuid_text_length;
                vec_res[dst_offset] = 0;
                ++dst_offset;
                offsets_res[i] = dst_offset;
            }
        }
        else if (const auto col_in = checkAndGetColumnConst<ColumnFixedString>(column.get()))
        {
            const auto column_fixed_string = checkAndGetColumn<ColumnFixedString>(&col_in->getDataColumn());
            if (!column_fixed_string || column_fixed_string->getN() != ipv6_bytes_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(ipv6_bytes_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            String data_in = col_in->getValue<String>();

            char buf[uuid_text_length];
            formatUUID(reinterpret_cast<const UInt8 *>(data_in.data()), reinterpret_cast<UInt8 *>(buf));

            block.getByPosition(result).column = DataTypeString().createConstColumn(col_in->size(), String(buf, uuid_text_length));
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionUUIDStringToNum : public IFunction
{
private:
    static void parseHex(const UInt8 * __restrict src, UInt8 * __restrict dst, const size_t num_bytes)
    {
        size_t src_pos = 0;
        size_t dst_pos = 0;
        for (; dst_pos < num_bytes; ++dst_pos)
        {
            dst[dst_pos] = unhex(src[src_pos]) * 16 + unhex(src[src_pos + 1]);
            src_pos += 2;
        }
    }

    static void parseUUID(const UInt8 * src36, UInt8 * dst16)
    {
        /// If string is not like UUID - implementation specific behaviour.

        parseHex(&src36[0], &dst16[0], 4);
        parseHex(&src36[9], &dst16[4], 2);
        parseHex(&src36[14], &dst16[6], 2);
        parseHex(&src36[19], &dst16[8], 2);
        parseHex(&src36[24], &dst16[10], 6);
    }

public:
    static constexpr auto name = "UUIDStringToNum";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionUUIDStringToNum>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        /// String or FixedString(36)
        if (!checkDataType<DataTypeString>(arguments[0].get()))
        {
            const auto ptr = checkAndGetDataType<DataTypeFixedString>(arguments[0].get());
            if (!ptr || ptr->getN() != uuid_text_length)
                throw Exception("Illegal type " + arguments[0]->getName() +
                                " of argument of function " + getName() +
                                ", expected FixedString(" + toString(uuid_text_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        }

        return std::make_shared<DataTypeFixedString>(uuid_bytes_length);
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnWithTypeAndName & col_type_name = block.getByPosition(arguments[0]);
        const ColumnPtr & column = col_type_name.column;

        if (const auto col_in = checkAndGetColumn<ColumnString>(column.get()))
        {
            const auto & vec_in = col_in->getChars();
            const auto & offsets_in = col_in->getOffsets();
            const size_t size = offsets_in.size();

            auto col_res = std::make_shared<ColumnFixedString>(uuid_bytes_length);
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            vec_res.resize(size * uuid_bytes_length);

            size_t src_offset = 0;
            size_t dst_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                /// If string has incorrect length - then return zero UUID.
                /// If string has correct length but contains something not like UUID - implementation specific behaviour.

                size_t string_size = offsets_in[i] - src_offset;
                if (string_size == uuid_text_length + 1)
                    parseUUID(&vec_in[src_offset], &vec_res[dst_offset]);
                else
                    memset(&vec_res[dst_offset], 0, uuid_bytes_length);

                dst_offset += uuid_bytes_length;
                src_offset += string_size;
            }
        }
        else if (const auto col_in = checkAndGetColumn<ColumnFixedString>(column.get()))
        {
            if (col_in->getN() != uuid_text_length)
                throw Exception("Illegal type " + col_type_name.type->getName() +
                                " of column " + col_in->getName() +
                                " argument of function " + getName() +
                                ", expected FixedString(" + toString(uuid_text_length) + ")",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

            const auto size = col_in->size();
            const auto & vec_in = col_in->getChars();

            auto col_res = std::make_shared<ColumnFixedString>(uuid_bytes_length);
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & vec_res = col_res->getChars();
            vec_res.resize(size * uuid_bytes_length);

            size_t src_offset = 0;
            size_t dst_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                parseUUID(&vec_in[src_offset], &vec_res[dst_offset]);
                src_offset += uuid_text_length;
                dst_offset += uuid_bytes_length;
            }
        }
        else if (const auto col_in = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            String data_in = col_in->getValue<String>();

            String res;

            if (data_in.size() == uuid_text_length)
            {
                char buf[uuid_bytes_length];
                parseUUID(reinterpret_cast<const UInt8 *>(data_in.data()), reinterpret_cast<UInt8 *>(buf));
                res.assign(buf, uuid_bytes_length);
            }
            else
                res.resize(uuid_bytes_length, '\0');

            block.getByPosition(result).column = DataTypeFixedString(uuid_bytes_length).createConstColumn(col_in->size(), res);
        }
        else
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
            + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionHex : public IFunction
{
public:
    static constexpr auto name = "hex";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionHex>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]) &&
            !checkDataType<DataTypeFixedString>(&*arguments[0]) &&
            !checkDataType<DataTypeDate>(&*arguments[0]) &&
            !checkDataType<DataTypeDateTime>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt8>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt16>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt32>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt64>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    template <typename T>
    void executeOneUInt(T x, char *& out)
    {
        bool was_nonzero = false;
        for (int offset = (sizeof(T) - 1) * 8; offset >= 0; offset -= 8)
        {
            UInt8 byte = static_cast<UInt8>((x >> offset) & 255);

            /// Leading zeros.
            if (byte == 0 && !was_nonzero && offset)
                continue;

            was_nonzero = true;

            *(out++) = hexUppercase(byte / 16);
            *(out++) = hexUppercase(byte % 16);
        }
        *(out++) = '\0';
    }

    template <typename T>
    bool tryExecuteUInt(const IColumn * col, ColumnPtr & col_res)
    {
        const ColumnVector<T> * col_vec = checkAndGetColumn<ColumnVector<T>>(col);
        const ColumnConst * col_const = checkAndGetColumnConst<ColumnVector<T>>(col);

        static constexpr size_t MAX_UINT_HEX_LENGTH = sizeof(T) * 2 + 1;    /// Including trailing zero byte.

        if (col_vec)
        {
            auto col_str = std::make_shared<ColumnString>();
            col_res = col_str;
            ColumnString::Chars_t & out_vec = col_str->getChars();
            ColumnString::Offsets_t & out_offsets = col_str->getOffsets();

            const typename ColumnVector<T>::Container_t & in_vec = col_vec->getData();

            size_t size = in_vec.size();
            out_offsets.resize(size);
            out_vec.resize(size * 3 + MAX_UINT_HEX_LENGTH); /// 3 is length of one byte in hex plus zero byte.

            size_t pos = 0;
            for (size_t i = 0; i < size; ++i)
            {
                /// Manual exponential growth, so as not to rely on the linear amortized work time of `resize` (no one guarantees it).
                if (pos + MAX_UINT_HEX_LENGTH > out_vec.size())
                    out_vec.resize(out_vec.size() * 2 + MAX_UINT_HEX_LENGTH);

                char * begin = reinterpret_cast<char *>(&out_vec[pos]);
                char * end = begin;
                executeOneUInt<T>(in_vec[i], end);

                pos += end - begin;
                out_offsets[i] = pos;
            }

            out_vec.resize(pos);

            return true;
        }
        else if (col_const)
        {
            char buf[MAX_UINT_HEX_LENGTH];
            char * pos = buf;
            executeOneUInt<T>(col_const->template getValue<T>(), pos);

            col_res = DataTypeString().createConstColumn(col_const->size(), String(buf));

            return true;
        }
        else
        {
            return false;
        }
    }

    void executeOneString(const UInt8 * pos, const UInt8 * end, char *& out)
    {
        while (pos < end)
        {
            UInt8 byte = *(pos++);
            *(out++) = hexUppercase(byte / 16);
            *(out++) = hexUppercase(byte % 16);
        }
        *(out++) = '\0';
    }

    bool tryExecuteString(const IColumn * col, ColumnPtr & col_res)
    {
        const ColumnString * col_str_in = checkAndGetColumn<ColumnString>(col);
        const ColumnConst * col_const_in = checkAndGetColumnConstStringOrFixedString(col);

        if (col_str_in)
        {
            auto col_str = std::make_shared<ColumnString>();
            col_res = col_str;
            ColumnString::Chars_t & out_vec = col_str->getChars();
            ColumnString::Offsets_t & out_offsets = col_str->getOffsets();

            const ColumnString::Chars_t & in_vec = col_str_in->getChars();
            const ColumnString::Offsets_t & in_offsets = col_str_in->getOffsets();

            size_t size = in_offsets.size();
            out_offsets.resize(size);
            out_vec.resize(in_vec.size() * 2 - size);

            char * begin = reinterpret_cast<char *>(&out_vec[0]);
            char * pos = begin;
            size_t prev_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                size_t new_offset = in_offsets[i];

                executeOneString(&in_vec[prev_offset], &in_vec[new_offset - 1], pos);

                out_offsets[i] = pos - begin;

                prev_offset = new_offset;
            }

            if (!out_offsets.empty() && out_offsets.back() != out_vec.size())
                throw Exception("Column size mismatch (internal logical error)", ErrorCodes::LOGICAL_ERROR);

            return true;
        }
        else if (col_const_in)
        {
            String src = col_const_in->getValue<String>();
            String res(src.size() * 2, '\0');
            char * pos = &res[0];
            const UInt8 * src_ptr = reinterpret_cast<const UInt8 *>(src.c_str());
            /// Let's write zero into res[res.size()]. Starting with C++ 11, this is correct.
            executeOneString(src_ptr, src_ptr + src.size(), pos);

            col_res = DataTypeString().createConstColumn(col_const_in->size(), res);

            return true;
        }
        else
        {
            return false;
        }
    }

    bool tryExecuteFixedString(const IColumn * col, ColumnPtr & col_res)
    {
        const ColumnFixedString * col_fstr_in = checkAndGetColumn<ColumnFixedString>(col);

        if (col_fstr_in)
        {
            auto col_str = std::make_shared<ColumnString>();

            col_res = col_str;

            ColumnString::Chars_t & out_vec = col_str->getChars();
            ColumnString::Offsets_t & out_offsets = col_str->getOffsets();

            const ColumnString::Chars_t & in_vec = col_fstr_in->getChars();

            size_t size = col_fstr_in->size();

            out_offsets.resize(size);
            out_vec.resize(in_vec.size() * 2 + size);

            char * begin = reinterpret_cast<char *>(&out_vec[0]);
            char * pos = begin;

            size_t n = col_fstr_in->getN();

            size_t prev_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                size_t new_offset = prev_offset + n;

                executeOneString(&in_vec[prev_offset], &in_vec[new_offset], pos);

                out_offsets[i] = pos - begin;
                prev_offset = new_offset;
            }

            if (!out_offsets.empty() && out_offsets.back() != out_vec.size())
                throw Exception("Column size mismatch (internal logical error)", ErrorCodes::LOGICAL_ERROR);

            return true;
        }
        else
        {
            return false;
        }
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const IColumn * column = block.getByPosition(arguments[0]).column.get();
        ColumnPtr & res_column = block.getByPosition(result).column;

        if (tryExecuteUInt<UInt8>(column, res_column) ||
            tryExecuteUInt<UInt16>(column, res_column) ||
            tryExecuteUInt<UInt32>(column, res_column) ||
            tryExecuteUInt<UInt64>(column, res_column) ||
            tryExecuteString(column, res_column) ||
            tryExecuteFixedString(column, res_column))
            return;

        throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
                        + " of argument of function " + getName(),
                        ErrorCodes::ILLEGAL_COLUMN);
    }
};


class FunctionUnhex : public IFunction
{
public:
    static constexpr auto name = "unhex";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionUnhex>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }

    void unhexOne(const char * pos, const char * end, char *& out)
    {
        if ((end - pos) & 1)
        {
            *out = unhex(*pos);
            ++out;
            ++pos;
        }
        while (pos < end)
        {
            UInt8 major = unhex(*pos);
            ++pos;
            UInt8 minor = unhex(*pos);
            ++pos;

            *out = (major << 4) | minor;
            ++out;
        }
        *out = '\0';
        ++out;
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const ColumnPtr & column = block.getByPosition(arguments[0]).column;

        if (const ColumnString * col = checkAndGetColumn<ColumnString>(column.get()))
        {
            std::shared_ptr<ColumnString> col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;

            ColumnString::Chars_t & out_vec = col_res->getChars();
            ColumnString::Offsets_t & out_offsets = col_res->getOffsets();

            const ColumnString::Chars_t & in_vec = col->getChars();
            const ColumnString::Offsets_t & in_offsets = col->getOffsets();

            size_t size = in_offsets.size();
            out_offsets.resize(size);
            out_vec.resize(in_vec.size() / 2 + size);

            char * begin = reinterpret_cast<char *>(&out_vec[0]);
            char * pos = begin;
            size_t prev_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                size_t new_offset = in_offsets[i];

                unhexOne(reinterpret_cast<const char *>(&in_vec[prev_offset]), reinterpret_cast<const char *>(&in_vec[new_offset - 1]), pos);

                out_offsets[i] = pos - begin;

                prev_offset = new_offset;
            }

            out_vec.resize(pos - begin);
        }
        else if(const ColumnConst * col = checkAndGetColumnConstStringOrFixedString(column.get()))
        {
            String src = col->getValue<String>();
            String res(src.size(), '\0');
            char * pos = &res[0];
            unhexOne(src.c_str(), src.c_str() + src.size(), pos);
            res = res.substr(0, pos - &res[0] - 1);

            block.getByPosition(result).column = DataTypeString().createConstColumn(col->size(), res);
        }
        else
        {
            throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
                            + " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_COLUMN);
        }
    }
};


class FunctionBitmaskToArray : public IFunction
{
public:
    static constexpr auto name = "bitmaskToArray";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionBitmaskToArray>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }
    bool isInjective(const Block &) override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeUInt8>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt16>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt32>(&*arguments[0]) &&
            !checkDataType<DataTypeUInt64>(&*arguments[0]) &&
            !checkDataType<DataTypeInt8>(&*arguments[0]) &&
            !checkDataType<DataTypeInt16>(&*arguments[0]) &&
            !checkDataType<DataTypeInt32>(&*arguments[0]) &&
            !checkDataType<DataTypeInt64>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeArray>(arguments[0]);
    }

    template <typename T>
    bool tryExecute(const IColumn * column, ColumnPtr & out_column)
    {
        if (const ColumnVector<T> * col_from = checkAndGetColumn<ColumnVector<T>>(column))
        {
            auto col_values = std::make_shared<ColumnVector<T>>();
            auto col_array = std::make_shared<ColumnArray>(col_values);
            out_column = col_array;

            ColumnArray::Offsets_t & res_offsets = col_array->getOffsets();
            typename ColumnVector<T>::Container_t & res_values = col_values->getData();

            const typename ColumnVector<T>::Container_t & vec_from = col_from->getData();
            size_t size = vec_from.size();
            res_offsets.resize(size);
            res_values.reserve(size * 2);

            for (size_t row = 0; row < size; ++row)
            {
                T x = vec_from[row];
                while (x)
                {
                    T y = (x & (x - 1));
                    T bit = x ^ y;
                    x = y;
                    res_values.push_back(bit);
                }
                res_offsets[row] = res_values.size();
            }

            return true;
        }
        else if (auto col_from = checkAndGetColumnConst<ColumnVector<T>>(column))
        {
            Array res;

            T x = col_from->template getValue<T>();
            for (size_t i = 0; i < sizeof(T) * 8; ++i)
            {
                T bit = static_cast<T>(1) << i;
                if (x & bit)
                {
                    res.push_back(static_cast<UInt64>(bit));
                }
            }

            out_column = DataTypeArray(std::make_shared<DataTypeNumber<T>>()).createConstColumn(col_from->size(), res);
            return true;
        }
        else
        {
            return false;
        }
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const IColumn * in_column = block.getByPosition(arguments[0]).column.get();
        ColumnPtr & out_column = block.getByPosition(result).column;

        if (tryExecute<UInt8>(in_column, out_column) ||
            tryExecute<UInt16>(in_column, out_column) ||
            tryExecute<UInt32>(in_column, out_column) ||
            tryExecute<UInt64>(in_column, out_column) ||
            tryExecute<Int8>(in_column, out_column) ||
            tryExecute<Int16>(in_column, out_column) ||
            tryExecute<Int32>(in_column, out_column) ||
            tryExecute<Int64>(in_column, out_column))
            return;

        throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
                        + " of first argument of function " + getName(),
                        ErrorCodes::ILLEGAL_COLUMN);
    }
};

class FunctionToStringCutToZero : public IFunction
{
public:
    static constexpr auto name = "toStringCutToZero";
    static FunctionPtr create(const Context & context) { return std::make_shared<FunctionToStringCutToZero>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 1; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!checkDataType<DataTypeFixedString>(&*arguments[0]) &&
            !checkDataType<DataTypeString>(&*arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of argument of function " + getName(),
            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeString>();
    }


    bool tryExecuteString(const IColumn * col, ColumnPtr & col_res)
    {
        const ColumnString * col_str_in = checkAndGetColumn<ColumnString>(col);
        const ColumnConst * col_const_in = checkAndGetColumnConstStringOrFixedString(col);

        if (col_str_in)
        {
            auto col_str = std::make_shared<ColumnString>();
            col_res = col_str;
            ColumnString::Chars_t & out_vec = col_str->getChars();
            ColumnString::Offsets_t & out_offsets = col_str->getOffsets();

            const ColumnString::Chars_t & in_vec = col_str_in->getChars();
            const ColumnString::Offsets_t & in_offsets = col_str_in->getOffsets();

            size_t size = in_offsets.size();
            out_offsets.resize(size);
            out_vec.resize(in_vec.size());

            char * begin = reinterpret_cast<char *>(&out_vec[0]);
            char * pos = begin;

            ColumnString::Offset_t current_in_offset = 0;

            for (size_t i = 0; i < size; ++i)
            {
                const char * pos_in = reinterpret_cast<const char *>(&in_vec[current_in_offset]);
                size_t current_size = strlen(pos_in);
                memcpySmallAllowReadWriteOverflow15(pos, pos_in, current_size);
                pos += current_size;
                *pos = '\0';
                ++pos;
                out_offsets[i] = pos - begin;
                current_in_offset = in_offsets[i];
            }
            out_vec.resize(pos - begin);

            if (!out_offsets.empty() && out_offsets.back() != out_vec.size())
                throw Exception("Column size mismatch (internal logical error)", ErrorCodes::LOGICAL_ERROR);

            return true;
        }
        else if (col_const_in)
        {
            std::string res(col_const_in->getValue<String>().c_str());
            col_res = DataTypeString().createConstColumn(col_const_in->size(), res);

            return true;
        }
        else
        {
            return false;
        }
    }

    bool tryExecuteFixedString(const IColumn * col, ColumnPtr & col_res)
    {
        const ColumnFixedString * col_fstr_in = checkAndGetColumn<ColumnFixedString>(col);

        if (col_fstr_in)
        {
            auto col_str = std::make_shared<ColumnString>();
            col_res = col_str;

            ColumnString::Chars_t & out_vec = col_str->getChars();
            ColumnString::Offsets_t & out_offsets = col_str->getOffsets();

            const ColumnString::Chars_t & in_vec = col_fstr_in->getChars();

            size_t size = col_fstr_in->size();

            out_offsets.resize(size);
            out_vec.resize(in_vec.size() + size);

            char * begin = reinterpret_cast<char *>(&out_vec[0]);
            char * pos = begin;
            const char * pos_in = reinterpret_cast<const char *>(&in_vec[0]);

            size_t n = col_fstr_in->getN();

            for (size_t i = 0; i < size; ++i)
            {
                size_t current_size = strnlen(pos_in, n);
                memcpySmallAllowReadWriteOverflow15(pos, pos_in, current_size);
                pos += current_size;
                *pos = '\0';
                out_offsets[i] = ++pos - begin;
                pos_in += n;
            }
            out_vec.resize(pos - begin);

            if (!out_offsets.empty() && out_offsets.back() != out_vec.size())
                throw Exception("Column size mismatch (internal logical error)", ErrorCodes::LOGICAL_ERROR);

            return true;
        }
        else
        {
            return false;
        }
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        const IColumn * column = block.getByPosition(arguments[0]).column.get();
        ColumnPtr & res_column = block.getByPosition(result).column;

        if (tryExecuteFixedString(column, res_column) || tryExecuteString(column, res_column))
            return;

        throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
                        + " of argument of function " + getName(),
                        ErrorCodes::ILLEGAL_COLUMN);
    }
};

}
