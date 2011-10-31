#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <DB/IO/WriteBufferFromFileDescriptor.h>


namespace DB
{

/** Принимает имя файла. Самостоятельно открывает и закрывает файл.
  */
class WriteBufferFromFile : public WriteBufferFromFileDescriptor
{
private:
	std::string file_name;
	
public:
	WriteBufferFromFile(const std::string & file_name_, int flags = -1, mode_t mode = 0666) : WriteBufferFromFileDescriptor(-1), file_name(file_name_)
	{
		fd = open(file_name.c_str(), flags == -1 ? O_WRONLY | O_APPEND | O_CREAT : flags, mode);
		
		if (-1 == fd)
			throwFromErrno("Cannot open file " + file_name, ErrorCodes::CANNOT_OPEN_FILE);
	}

    virtual ~WriteBufferFromFile()
	{
		next();
		if (0 != close(fd))
			throwFromErrno("Cannot close file " + file_name, ErrorCodes::CANNOT_CLOSE_FILE);
	}

	virtual std::string getFileName()
	{
		return file_name;
	}
};

}
