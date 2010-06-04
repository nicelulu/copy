#ifndef DBMS_COMMON_COMPRESSED_WRITEBUFFER_H
#define DBMS_COMMON_COMPRESSED_WRITEBUFFER_H

#include <quicklz/quicklz_level1.h>

#include <DB/IO/WriteBuffer.h>
#include <DB/IO/CompressedStream.h>


namespace DB
{

class CompressedWriteBuffer : public WriteBuffer
{
private:
	WriteBuffer & out;

	char compressed_buffer[DBMS_COMPRESSING_STREAM_BUFFER_SIZE + QUICKLZ_ADDITIONAL_SPACE];
	char scratch[QLZ_SCRATCH_COMPRESS];

public:
	CompressedWriteBuffer(WriteBuffer & out_) : out(out_) {}

	void next()
	{
		size_t compressed_size = qlz_compress(
			internal_buffer,
			compressed_buffer,
			pos - internal_buffer,
			scratch);

		out.write(compressed_buffer, compressed_size);
		pos = internal_buffer;
	}

	~CompressedWriteBuffer()
	{
		next();
	}
};

}

#endif
