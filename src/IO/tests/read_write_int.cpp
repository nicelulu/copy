#include <string>

#include <iostream>

#include <common/types.h>
#include <IO/ReadBufferFromString.h>
#include <IO/WriteBufferFromString.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>


int main(int, char **)
{
    try
    {
        Int64 x1 = std::numeric_limits<Int64>::min();
        Int64 x2 = 0;
        std::string s;

        std::cerr << static_cast<Int64>(x1) << std::endl;

        {
            RK::WriteBufferFromString wb(s);
            RK::writeIntText(x1, wb);
        }

        std::cerr << s << std::endl;

        {
            RK::ReadBufferFromString rb(s);
            RK::readIntText(x2, rb);
        }

        std::cerr << static_cast<Int64>(x2) << std::endl;
    }
    catch (const RK::Exception & e)
    {
        std::cerr << e.what() << ", " << e.displayText() << std::endl;
        return 1;
    }

    return 0;
}
