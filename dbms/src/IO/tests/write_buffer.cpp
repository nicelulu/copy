#include <string>

#include <iostream>
#include <sstream>

#include <DB/Core/Types.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/WriteBufferFromOStream.h>


int main(int argc, char ** argv)
{
	try
	{
		DB::Int64 a = -123456;
		DB::Float64 b = 123.456;
		DB::String c = "вася пе\tтя";
		DB::String d = "'xyz\\";

		std::stringstream s;

		{
			DB::WriteBufferFromOStream out(s);

			DB::writeIntText(a, out);
			DB::writeChar(' ', out);
			
			DB::writeFloatText(b, out);
			DB::writeChar(' ', out);
			
			DB::writeEscapedString(c, out);
			DB::writeChar('\t', out);

			DB::writeQuotedString(d, out);
			DB::writeChar('\n', out);
		}

		std::cout << s.str();
	}
	catch (const DB::Exception & e)
	{
		std::cerr << e.what() << ", " << e.message() << std::endl;
		return 1;
	}

	return 0;
}
