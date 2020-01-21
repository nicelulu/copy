#include <IO/ReadHelpers.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int CANNOT_PARSE_NUMBER;
    extern const int ARGUMENT_OUT_OF_BOUND;
}


template <typename T>
inline void readDigits(ReadBuffer & buf, T & x, unsigned int & digits, int & exponent, bool digits_only = false)
{
    x = 0;
    exponent = 0;
    unsigned int max_digits = digits;
    digits = 0;
    unsigned int places = 0;
    typename T::NativeType sign = 1;
    bool leading_zeroes = true;
    bool after_point = false;

    if (buf.eof())
        throwReadAfterEOF();

    if (!buf.eof())
    {
        switch (*buf.position())
        {
            case '-':
                sign = -1;
                [[fallthrough]];
            case '+':
                ++buf.position();
                break;
        }
    }

    bool stop = false;
    while (!buf.eof() && !stop)
    {
        const char & byte = *buf.position();
        switch (byte)
        {
            case '.':
                after_point = true;
                leading_zeroes = false;
                break;
            case '0':
            {
                if (leading_zeroes)
                    break;

                if (after_point)
                {
                    ++places; /// Count trailing zeroes. They would be used only if there's some other digit after them.
                    break;
                }
                [[fallthrough]];
            }
            case '1': [[fallthrough]];
            case '2': [[fallthrough]];
            case '3': [[fallthrough]];
            case '4': [[fallthrough]];
            case '5': [[fallthrough]];
            case '6': [[fallthrough]];
            case '7': [[fallthrough]];
            case '8': [[fallthrough]];
            case '9':
            {
                leading_zeroes = false;

                ++places; // num zeroes before + current digit
                if (digits + places > max_digits)
                    throw Exception("Too many digits (" + std::to_string(digits + places) + " > " + std::to_string(max_digits)
                            + ") in decimal value", ErrorCodes::ARGUMENT_OUT_OF_BOUND);

                digits += places;
                if (after_point)
                    exponent -= places;

                // TODO: accurate shift10 for big integers
                for (; places; --places)
                    x *= 10;
                x += (byte - '0');
                break;
            }
            case 'e': [[fallthrough]];
            case 'E':
            {
                ++buf.position();
                Int32 addition_exp = 0;
                readIntText(addition_exp, buf);
                exponent += addition_exp;
                stop = true;
                continue;
            }

            default:
                if (digits_only)
                    throw Exception("Unexpected symbol while reading decimal", ErrorCodes::CANNOT_PARSE_NUMBER);
                stop = true;
                continue;
        }
        ++buf.position();
    }

    x *= sign;
}

template <typename T>
inline void readDecimalText(ReadBuffer & buf, T & x, unsigned int precision, unsigned int & scale, bool digits_only = false)
{
    unsigned int digits = precision;
    int exponent;
    readDigits(buf, x, digits, exponent, digits_only);

    if (static_cast<int>(digits) + exponent > static_cast<int>(precision - scale))
        throw Exception("Decimal value is too big", ErrorCodes::ARGUMENT_OUT_OF_BOUND);
    if (static_cast<int>(scale) + exponent < 0)
        throw Exception("Decimal value is too small", ErrorCodes::ARGUMENT_OUT_OF_BOUND);

    scale += exponent;
}

template <typename T>
inline bool tryReadDecimalText(ReadBuffer & buf, T & x, unsigned int precision, unsigned int & scale)
{
    unsigned int digits = precision;
    int exponent;

    try
    {
        readDigits(buf, x, digits, exponent, true);
        if (static_cast<int>(digits) + exponent > static_cast<int>(precision - scale) ||
            static_cast<int>(scale) + exponent < 0)
            return false;
    }
    catch (const Exception &)
    {
        return false;
    }

    scale += exponent;
    return true;
}

template <typename T>
inline void readCSVDecimalText(ReadBuffer & buf, T & x, unsigned int precision, unsigned int & scale)
{
    if (buf.eof())
        throwReadAfterEOF();

    char maybe_quote = *buf.position();

    if (maybe_quote == '\'' || maybe_quote == '\"')
        ++buf.position();

    readDecimalText(buf, x, precision, scale, false);

    if (maybe_quote == '\'' || maybe_quote == '\"')
        assertChar(maybe_quote, buf);
}

}
