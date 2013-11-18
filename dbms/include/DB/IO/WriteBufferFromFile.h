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
	WriteBufferFromFile(const std::string & file_name_, size_t buf_size = DBMS_DEFAULT_BUFFER_SIZE, int flags = -1, mode_t mode = 0666)
		: WriteBufferFromFileDescriptor(-1, buf_size), file_name(file_name_)
	{
		fd = open(file_name.c_str(), flags == -1 ? O_WRONLY | O_TRUNC | O_CREAT : flags, mode);
		
		if (-1 == fd)
			throwFromErrno("Cannot open file " + file_name, errno == ENOENT ? ErrorCodes::FILE_DOESNT_EXIST : ErrorCodes::CANNOT_OPEN_FILE);
	}

	~WriteBufferFromFile()
	{
		try
		{
			next();
		}
		catch (...)
		{
		}

		close(fd);
	}
	
	/** fsync() transfers ("flushes") all modified in-core data of (i.e., modified buffer cache pages for) the file
	  * referred to by the file descriptor fd to the disk device (or other permanent storage device)
	  * so that all changed information can be retrieved even after the system crashed or was rebooted.
	  * This includes writing through or flushing a disk cache if present. The call blocks until the device
	  * reports that the transfer has completed. It also flushes metadata information associated with the file (see stat(2)).
	  *    - man fsync */
	void sync()
	{
		fsync(fd);
	}

	virtual std::string getFileName()
	{
		return file_name;
	}
};

}
