#pragma once
#include <IO/ReadBuffer.h>
#include <IO/BufferWithOwnMemory.h>

namespace DB
{

namespace ErrorCodes
{
extern const int MEMORY_LIMIT_EXCEEDED;
}

/// Allows to peek next part of data from sub-buffer without extracting it
class PeekableReadBuffer : public BufferWithOwnMemory<ReadBuffer>
{
public:
    constexpr static size_t default_limit = 32 * DBMS_DEFAULT_BUFFER_SIZE;

    explicit PeekableReadBuffer(ReadBuffer & sub_buf_, size_t unread_limit_ = default_limit)
        : sub_buf(sub_buf_), unread_limit(unread_limit_), peeked_size(0)
    {
        /// Read from sub-buffer
        Buffer & sub_working = sub_buf.buffer();
        BufferBase::set(sub_working.begin(), sub_working.size(), sub_buf.offset());
    }

    bool peekNext()
    {
        if (!readFromOwnMemory())
        {
            bytes += pos - sub_buf.buffer().begin();
            sub_buf.position() = pos;
        }
        size_t available = sub_buf.available();
        if (!available)
        {
            bool res = sub_buf.next();
            if (!readFromOwnMemory())
                BufferBase::set(sub_buf.buffer().begin(), sub_buf.buffer().size(), sub_buf.offset());
            return res;
        }

        size_t offset = resizeOwnMemoryIfNecessary(available);

        /// Save unread data from sub-buffer to own memory
        memcpy(memory.data() + peeked_size, sub_buf.position(), available);
        peeked_size += available;
        /// Switch to reading from own memory (or just update size if already switched)
        BufferBase::set(memory.data(), peeked_size, offset);

        sub_buf.position() += available;
        return sub_buf.next();
    }

    Buffer & lastPeeked()
    {
        return sub_buf.buffer();
    }

private:
    bool nextImpl() override
    {
        bool res = true;
        if (readFromOwnMemory())
        {
            /// All copied data have been read from own memory, continue reading from sub_buf
            peeked_size = 0;
        }
        else
        {
            /// Load next data to sub_buf
            sub_buf.position() = pos;
            res = sub_buf.next();
        }

        Buffer & sub_working = sub_buf.buffer();
        /// Switch to reading from sub_buf (or just update it if already switched)
        BufferBase::set(sub_working.begin(), sub_working.size(), 0);
        return res;
    }

    inline bool readFromOwnMemory() const
    {
        return peeked_size;
    }

    size_t resizeOwnMemoryIfNecessary(size_t bytes_to_append)
    {
        size_t offset = readFromOwnMemory() ? this->offset() : 0;
        size_t new_size = peeked_size + bytes_to_append;
        if (memory.size() < new_size)
        {
            if (bytes_to_append < offset && 2 * (peeked_size - offset) <= memory.size())
            {
                /// Move unread data to the beginning of own memory instead of resize own memory
                peeked_size -= offset;
                memmove(memory.data(), memory.data() + offset, peeked_size);
                bytes += offset;
                return 0;
            }
            else
            {
                if (unread_limit < new_size)
                    throw DB::Exception("trying to peek too much data", ErrorCodes::MEMORY_LIMIT_EXCEEDED);
                memory.resize(new_size);
            }
        }
        return offset;
    }

    ReadBuffer & sub_buf;
    const size_t unread_limit;
    size_t peeked_size;
};

}
