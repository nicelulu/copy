#include <string>

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <Poco/Stopwatch.h>

#include <DB/Core/Types.h>
#include <DB/IO/WriteBufferFromOStream.h>
#include <DB/IO/ReadBufferFromIStream.h>
#include <DB/IO/CompressedWriteBuffer.h>
#include <DB/IO/CompressedReadBuffer.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/ReadHelpers.h>


int main(int argc, char ** argv)
{
	try
	{
		std::cout << std::fixed << std::setprecision(2);
		
		size_t n = 100000000;
		Poco::Stopwatch stopwatch;
	
		{
			std::ofstream ostr("test1");
			
			DB::WriteBufferFromOStream buf(ostr);
			DB::CompressedWriteBuffer compressed_buf(buf);

			stopwatch.restart();
			for (size_t i = 0; i < n; ++i)
			{
				DB::writeIntText(i, compressed_buf);
				DB::writeChar('\t', compressed_buf);
			}
			stopwatch.stop();
			std::cout << "Writing done (1). Elapsed: " << static_cast<double>(stopwatch.elapsed()) / 1000000
				<< ", " << (static_cast<double>(compressed_buf.count()) / stopwatch.elapsed()) << " MB/s"
				<< std::endl;
		}

		{
			std::ifstream istr("test1");
			DB::ReadBufferFromIStream buf(istr);
			DB::CompressedReadBuffer compressed_buf(buf);
			std::string s;

			stopwatch.restart();
			for (size_t i = 0; i < n; ++i)
			{
				size_t x;
				DB::readIntText(x, compressed_buf);
				compressed_buf.ignore();
				
				if (x != i)
				{
					std::stringstream s;
					s << "Failed!, read: " << x << ", expected: " << i;
					throw DB::Exception(s.str());
				}
			}
			stopwatch.stop();
			std::cout << "Reading done (1). Elapsed: " << static_cast<double>(stopwatch.elapsed()) / 1000000
				<< ", " << (static_cast<double>(compressed_buf.count()) / stopwatch.elapsed()) << " MB/s"
				<< std::endl;
		}
	}
	catch (const DB::Exception & e)
	{
		std::cerr << e.what() << ", " << e.message() << std::endl;
		return 1;
	}

	return 0;
}
