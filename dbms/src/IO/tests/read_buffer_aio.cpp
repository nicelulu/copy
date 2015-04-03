#include <DB/IO/ReadBufferAIO.h>
#include <DB/Core/Defines.h>

#include <boost/filesystem.hpp>

#include <vector>
#include <iostream>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <unistd.h>

namespace
{

void run();
void prepare(size_t s, std::string & directory, std::string  & filename, std::string & buf);
void prepare2(std::string & directory, std::string  & filename, std::string & buf);
void prepare3(std::string & directory, std::string  & filename, std::string & buf);
void prepare4(std::string & directory, std::string  & filename, std::string & buf);
void die(const std::string & msg);
void run_test(unsigned int num, const std::function<bool()> func);

bool test1(const std::string & filename);
bool test2(const std::string & filename, const std::string & buf);
bool test3(const std::string & filename, const std::string & buf);
bool test4(const std::string & filename, const std::string & buf);
bool test5(const std::string & filename, const std::string & buf);
bool test6(const std::string & filename, const std::string & buf);
bool test7(const std::string & filename, const std::string & buf);
bool test8(const std::string & filename, const std::string & buf);
bool test9(const std::string & filename, const std::string & buf);
bool test10(const std::string & filename, const std::string & buf);
bool test11(const std::string & filename);
bool test12(const std::string & filename, const std::string & buf);
bool test13(const std::string & filename, const std::string & buf);
bool test14(const std::string & filename, const std::string & buf);
bool test15(const std::string & filename, const std::string & buf);
bool test16(const std::string & filename, const std::string & buf);
bool test17(const std::string & filename, const std::string & buf);
bool test18(const std::string & filename, const std::string & buf);

void run()
{
	namespace fs = boost::filesystem;

	std::string directory;
	std::string filename;
	std::string buf;
	prepare(10 * DEFAULT_AIO_FILE_BLOCK_SIZE, directory, filename, buf);

	std::string directory2;
	std::string filename2;
	std::string buf2;
	prepare(2 * DEFAULT_AIO_FILE_BLOCK_SIZE - 3, directory2, filename2, buf2);

	std::string directory3;
	std::string filename3;
	std::string buf3;
	prepare2(directory3, filename3, buf3);

	std::string directory4;
	std::string filename4;
	std::string buf4;
	prepare3(directory4, filename4, buf4);

	std::string directory5;
	std::string filename5;
	std::string buf5;
	prepare4(directory5, filename5, buf5);

	const std::vector<std::function<bool()> > tests =
	{
		std::bind(test1, std::ref(filename)),
		std::bind(test2, std::ref(filename), std::ref(buf)),
		std::bind(test3, std::ref(filename), std::ref(buf)),
		std::bind(test4, std::ref(filename), std::ref(buf)),
		std::bind(test5, std::ref(filename), std::ref(buf)),
		std::bind(test6, std::ref(filename), std::ref(buf)),
		std::bind(test7, std::ref(filename), std::ref(buf)),
		std::bind(test8, std::ref(filename), std::ref(buf)),
		std::bind(test9, std::ref(filename), std::ref(buf)),
		std::bind(test10, std::ref(filename), std::ref(buf)),
		std::bind(test11, std::ref(filename)),
		std::bind(test12, std::ref(filename), std::ref(buf)),
		std::bind(test13, std::ref(filename2), std::ref(buf2)),
		std::bind(test14, std::ref(filename), std::ref(buf)),
		std::bind(test15, std::ref(filename3), std::ref(buf3)),
		std::bind(test16, std::ref(filename3), std::ref(buf3)),
		std::bind(test17, std::ref(filename4), std::ref(buf4)),
		std::bind(test18, std::ref(filename5), std::ref(buf5))
	};

	unsigned int num = 0;
	for (const auto & test : tests)
	{
		++num;
		run_test(num, test);
	}

	fs::remove_all(directory);
	fs::remove_all(directory2);
	fs::remove_all(directory3);
	fs::remove_all(directory4);
	fs::remove_all(directory5);
}

void prepare(size_t s, std::string & directory, std::string  & filename, std::string & buf)
{
	static const std::string symbols = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	char pattern[] = "/tmp/fileXXXXXX";
	char * dir = ::mkdtemp(pattern);
	if (dir == nullptr)
		die("Could not create directory");

	directory = std::string(dir);
	filename = directory + "/foo";

	size_t n = 10 * DEFAULT_AIO_FILE_BLOCK_SIZE;
	buf.reserve(n);

	for (size_t i = 0; i < n; ++i)
		buf += symbols[i % symbols.length()];

	std::ofstream out(filename.c_str());
	if (!out.is_open())
		die("Could not open file");

	out << buf;
}

void prepare2(std::string & directory, std::string & filename, std::string & buf)
{
	char pattern[] = "/tmp/fileXXXXXX";
	char * dir = ::mkdtemp(pattern);
	if (dir == nullptr)
		die("Could not create directory");

	directory = std::string(dir);
	filename = directory + "/foo";

	buf = "122333444455555666666777777788888888999999999";

	std::ofstream out(filename.c_str());
	if (!out.is_open())
		die("Could not open file");

	out << buf;
}

void prepare3(std::string & directory, std::string & filename, std::string & buf)
{
	char pattern[] = "/tmp/fileXXXXXX";
	char * dir = ::mkdtemp(pattern);
	if (dir == nullptr)
		die("Could not create directory");

	directory = std::string(dir);
	filename = directory + "/foo";

	buf = "122333444455555666666777777788888888999999999";

	std::ofstream out(filename.c_str());
	if (!out.is_open())
		die("Could not open file");

	out.seekp(7, std::ios_base::beg);
	out << buf;
}

void prepare4(std::string & directory, std::string & filename, std::string & buf)
{
	static const std::string symbols = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	char pattern[] = "/tmp/fileXXXXXX";
	char * dir = ::mkdtemp(pattern);
	if (dir == nullptr)
		die("Could not create directory");

	directory = std::string(dir);
	filename = directory + "/foo";

	std::ofstream out(filename.c_str());
	if (!out.is_open())
		die("Could not open file");

	for (size_t i = 0; i < 1340; ++i)
		buf += symbols[i % symbols.length()];

	out.seekp(2984, std::ios_base::beg);
	out << buf;
}

void die(const std::string & msg)
{
	std::cout << msg << "\n";
	::exit(EXIT_FAILURE);
}

void run_test(unsigned int num, const std::function<bool()> func)
{
	bool ok;

	try
	{
		ok = func();
	}
	catch (const DB::Exception & ex)
	{
		ok = false;
		std::cout << "Caught exception " << ex.displayText() << "\n";
	}
	catch (const std::exception & ex)
	{
		ok = false;
		std::cout << "Caught exception " << ex.what() << "\n";
	}

	if (ok)
		std::cout << "Test " << num << " passed\n";
	else
		std::cout << "Test " << num << " failed\n";
}

bool test1(const std::string & filename)
{
	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	if (in.getFileName() != filename)
		return false;
	if (in.getFD() == -1)
		return false;
	return true;
}

bool test2(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(buf.length());

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	size_t count = in.read(&newbuf[0], newbuf.length());
	if (count != newbuf.length())
		return false;

	return (newbuf == buf);
}

bool test3(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(buf.length());

	size_t requested = 9 * DEFAULT_AIO_FILE_BLOCK_SIZE;

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	in.setMaxBytes(requested);
	size_t count = in.read(&newbuf[0], newbuf.length());

	newbuf.resize(count);
	return (newbuf == buf.substr(0, requested));
}

bool test4(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(buf.length());

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	in.setMaxBytes(0);
	size_t n_read = in.read(&newbuf[0], newbuf.length());

	return n_read == 0;
}

bool test5(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(1 + (DEFAULT_AIO_FILE_BLOCK_SIZE >> 1));

	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);
	in.setMaxBytes(1 + (DEFAULT_AIO_FILE_BLOCK_SIZE >> 1));

	size_t count = in.read(&newbuf[0], newbuf.length());
	if (count != newbuf.length())
		return false;

	if (newbuf != buf.substr(0, newbuf.length()))
		return false;

	return true;
}

bool test6(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(buf.length());

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);

	if (in.getPositionInFile() != 0)
		return false;

	size_t count = in.read(&newbuf[0], newbuf.length());
	if (count != newbuf.length())
		return false;

	if (static_cast<size_t>(in.getPositionInFile()) != buf.length())
		return false;

	return true;
}

bool test7(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(buf.length() - DEFAULT_AIO_FILE_BLOCK_SIZE);

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	(void) in.seek(DEFAULT_AIO_FILE_BLOCK_SIZE, SEEK_SET);
	size_t count = in.read(&newbuf[0], newbuf.length());
	if (count != (9 * DEFAULT_AIO_FILE_BLOCK_SIZE))
		return false;

	return (newbuf == buf.substr(DEFAULT_AIO_FILE_BLOCK_SIZE));
}

bool test8(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(DEFAULT_AIO_FILE_BLOCK_SIZE - 1);

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	(void) in.seek(DEFAULT_AIO_FILE_BLOCK_SIZE + 1, SEEK_CUR);
	size_t count = in.read(&newbuf[0], newbuf.length());

	if (count != newbuf.length())
		return false;

	if (newbuf != buf.substr(DEFAULT_AIO_FILE_BLOCK_SIZE + 1, newbuf.length()))
		return false;

	return true;
}

bool test9(const std::string & filename, const std::string & buf)
{
	bool ok = false;

	try
	{
		std::string newbuf;
		newbuf.resize(buf.length());

		DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
		size_t count =  in.read(&newbuf[0], newbuf.length());
		if (count != newbuf.length())
			return false;
		in.setMaxBytes(9 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	}
	catch (const DB::Exception &)
	{
		ok = true;
	}

	return ok;
}

bool test10(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(4 * DEFAULT_AIO_FILE_BLOCK_SIZE);

	DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
	size_t count1 = in.read(&newbuf[0], newbuf.length());
	if (count1 != newbuf.length())
		return false;

	if (newbuf != buf.substr(0, 4 * DEFAULT_AIO_FILE_BLOCK_SIZE))
		return false;

	(void) in.seek(2 * DEFAULT_AIO_FILE_BLOCK_SIZE, SEEK_CUR);

	size_t count2 = in.read(&newbuf[0], newbuf.length());
	if (count2 != newbuf.length())
		return false;

	if (newbuf != buf.substr(6 * DEFAULT_AIO_FILE_BLOCK_SIZE))
		return false;

	return true;
}

bool test11(const std::string & filename)
{
	bool ok = false;

	try
	{
		DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
		(void) in.seek(-DEFAULT_AIO_FILE_BLOCK_SIZE, SEEK_SET);
	}
	catch (const DB::Exception &)
	{
		ok = true;
	}

	return ok;
}

bool test12(const std::string & filename, const std::string & buf)
{
	bool ok = false;

	try
	{
		std::string newbuf;
		newbuf.resize(4 * DEFAULT_AIO_FILE_BLOCK_SIZE);

		DB::ReadBufferAIO in(filename, 3 * DEFAULT_AIO_FILE_BLOCK_SIZE);
		size_t count =  in.read(&newbuf[0], newbuf.length());
		if (count != newbuf.length())
			return false;

		(void) in.seek(-(10 * DEFAULT_AIO_FILE_BLOCK_SIZE), SEEK_CUR);
	}
	catch (const DB::Exception &)
	{
		ok = true;
	}

	return ok;
}

bool test13(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(2 * DEFAULT_AIO_FILE_BLOCK_SIZE - 3);

	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);
	size_t count1 = in.read(&newbuf[0], newbuf.length());
	if (count1 != newbuf.length())
		return false;
	return true;
}

bool test14(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(1 + (DEFAULT_AIO_FILE_BLOCK_SIZE >> 1));

	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);
	(void) in.seek(2, SEEK_SET);
	in.setMaxBytes(3 + (DEFAULT_AIO_FILE_BLOCK_SIZE >> 1));

	size_t count = in.read(&newbuf[0], newbuf.length());
	if (count != newbuf.length())
		return false;

	if (newbuf != buf.substr(2, newbuf.length()))
		return false;

	return true;
}

bool test15(const std::string & filename, const std::string & buf)
{
	std::string newbuf;
	newbuf.resize(1000);

	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);

	size_t count = in.read(&newbuf[0], 1);
	if (count != 1)
		return false;
	if (newbuf[0] != '1')
		return false;
	return true;
}

bool test16(const std::string & filename, const std::string & buf)
{
	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);
	size_t count;

	{
		std::string newbuf;
		newbuf.resize(1);
		count = in.read(&newbuf[0], 1);
		if (count != 1)
			return false;
		if (newbuf[0] != '1')
			return false;
	}

	in.seek(2, SEEK_CUR);

	{
		std::string newbuf;
		newbuf.resize(3);
		count = in.read(&newbuf[0], 3);
		if (count != 3)
			return false;
		if (newbuf != "333")
			return false;
	}

	in.seek(4, SEEK_CUR);

	{
		std::string newbuf;
		newbuf.resize(5);
		count = in.read(&newbuf[0], 5);
		if (count != 5)
			return false;
		if (newbuf != "55555")
			return false;
	}

	in.seek(6, SEEK_CUR);

	{
		std::string newbuf;
		newbuf.resize(7);
		count = in.read(&newbuf[0], 7);
		if (count != 7)
			return false;
		if (newbuf != "7777777")
			return false;
	}

	in.seek(8, SEEK_CUR);

	{
		std::string newbuf;
		newbuf.resize(9);
		count = in.read(&newbuf[0], 9);
		if (count != 9)
			return false;
		if (newbuf != "999999999")
			return false;
	}

	return true;
}

bool test17(const std::string & filename, const std::string & buf)
{
	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);
	size_t count;

	{
		std::string newbuf;
		newbuf.resize(10);
		count = in.read(&newbuf[0], 10);

		if (count != 10)
			return false;
		if (newbuf.substr(0, 7) != std::string(7, '\0'))
			return false;
		if (newbuf.substr(7) != "122")
			return false;
	}

	in.seek(7 + buf.length() - 2, SEEK_SET);

	{
		std::string newbuf;
		newbuf.resize(160);
		count = in.read(&newbuf[0], 160);

		if (count != 2)
			return false;
		if (newbuf.substr(0, 2) != "99")
			return false;
	}

	in.seek(7 + buf.length() + DEFAULT_AIO_FILE_BLOCK_SIZE, SEEK_SET);

	{
		std::string newbuf;
		newbuf.resize(50);
		count = in.read(&newbuf[0], 50);
		if (count != 0)
			return false;
	}

	return true;
}

bool test18(const std::string & filename, const std::string & buf)
{
	DB::ReadBufferAIO in(filename, DEFAULT_AIO_FILE_BLOCK_SIZE);

	std::string newbuf;
	newbuf.resize(1340);

	in.seek(2984, SEEK_SET);
	size_t count = in.read(&newbuf[0], 1340);

	if (count != 1340)
		return false;
	if (newbuf != buf)
		return false;

	return true;
}

}

int main()
{
	run();
	return 0;
}

