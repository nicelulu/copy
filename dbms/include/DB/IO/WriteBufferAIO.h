#pragma once

#include <DB/IO/WriteBufferFromFileBase.h>
#include <DB/IO/WriteBuffer.h>
#include <DB/IO/BufferWithOwnMemory.h>
#include <DB/Core/Defines.h>
#include <statdaemons/AIO.h>

#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace DB
{

/** Класс для асинхронной записи данных.
  */
class WriteBufferAIO : public WriteBufferFromFileBase
{
public:
	WriteBufferAIO(const std::string & filename_, size_t buffer_size_ = DBMS_DEFAULT_BUFFER_SIZE, int flags_ = -1, mode_t mode_ = 0666,
		char * existing_memory_ = nullptr);
	~WriteBufferAIO() override;

	WriteBufferAIO(const WriteBufferAIO &) = delete;
	WriteBufferAIO & operator=(const WriteBufferAIO &) = delete;

	off_t getPositionInFile() override;
	void truncate(off_t length = 0) override;
	void sync() override;
	std::string getFileName() const noexcept override { return filename; }
	int getFD() const noexcept override { return fd; }

private:
	///
	off_t doSeek(off_t off, int whence) override;
	/// Если в буфере ещё остались данные - запишем их.
	void flush();
	///
	void nextImpl() override;
	/// Ждать окончания текущей асинхронной задачи.
	bool waitForAIOCompletion();
	/// Менять местами основной и дублирующий буферы.
	void swapBuffers() noexcept;
	///
	void prepare();
	///
	void finalize();

private:
	/// Буфер для асинхронных операций записи данных.
	BufferWithOwnMemory<WriteBuffer> flush_buffer;

	/// Описание асинхронного запроса на запись.
	iocb request;
	std::vector<iocb *> request_ptrs{&request};
	std::vector<io_event> events{1};

	AIOContext aio_context{1};

	iovec iov[3];

	/// Дополнительный буфер размером со страницу. Содежрит те данные, которые
	/// не влезают в основной буфер.
	Memory memory_page{DEFAULT_AIO_FILE_BLOCK_SIZE, DEFAULT_AIO_FILE_BLOCK_SIZE};

	const std::string filename;

	/// Количество байтов, которые будут записаны на диск.
	off_t bytes_to_write = 0;

	/// Количество нулевых байтов, которые надо отрезать c конца файла
	/// после завершения операции записи данных.
	off_t truncation_count = 0;

	/// Текущая позиция в файле.
	off_t pos_in_file = 0;
	/// Максимальная достигнутая позиция в файле.
	off_t max_pos_in_file = 0;

	Position buffer_begin = nullptr;
	size_t excess_count = 0;
	size_t buffer_capacity = 0;
	size_t region_aligned_size = 0;
	off_t region_aligned_begin = 0;
	off_t bytes_written = 0;

	/// Файловый дескриптор для записи.
	int fd = -1;
	/// Файловый дескриптор для чтения. Употребляется для невыровненных записей.
	int fd2 = -1;

	/// Асинхронная операция записи ещё не завершилась?
	bool is_pending_write = false;
	/// Было получено исключение?
	bool got_exception = false;
};

}
