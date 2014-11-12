#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <iostream>

#include <Poco/DirectoryIterator.h>
#include <Poco/File.h>

#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>


static void localBackupImpl(Poco::Path source_path, Poco::Path destination_path, size_t level)
{
	if (level >= 1000)
		throw DB::Exception("Too deep recursion");

	std::cerr << source_path.toString() << ", " << destination_path.toString() << "\n";

	Poco::File(destination_path).createDirectories();

	Poco::DirectoryIterator dir_end;
	for (Poco::DirectoryIterator dir_it(source_path); dir_it != dir_end; ++dir_it)
	{
		Poco::Path source = dir_it.path();
		Poco::Path destination = destination_path;
		destination.append(dir_it.name());

		if (!dir_it->isDirectory())
		{
			dir_it->setReadOnly();

			std::string source_str = source.toString();
			std::string destination_str = destination.toString();

			/** Пытаемся создать hard link.
			  * Если он уже существует, то проверим, что source и destination указывают на один и тот же inode.
			  */
			if (0 != link(source_str.c_str(), destination_str.c_str()))
			{
				if (errno == EEXIST)
				{
					auto link_errno = errno;

					struct stat source_descr;
					struct stat destination_descr;

					if (0 != lstat(source_str.c_str(), &source_descr))
						DB::throwFromErrno("Cannot stat " + source_str);

					if (0 != lstat(destination_str.c_str(), &destination_descr))
						DB::throwFromErrno("Cannot stat " + destination_str);

					if (source_descr.st_ino != destination_descr.st_ino)
						DB::throwFromErrno("Destination file " + destination_str + " is already exist and have different inode.", 0, link_errno);
				}
				else
					DB::throwFromErrno("Cannot link " + source_str + " to " + destination_str);
			}
		}
		else
		{
			localBackupImpl(source, destination, level + 1);
		}
	}
}

void localBackup(Poco::Path source_path, Poco::Path destination_path)
{
	localBackupImpl(source_path, destination_path, 0);
}
