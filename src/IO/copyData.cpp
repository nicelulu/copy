/**
 * Copyright 2016-2023 ClickHouse, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <Common/Exception.h>
#include <IO/ReadBuffer.h>
#include <IO/WriteBuffer.h>
#include <IO/copyData.h>


namespace RK
{
namespace ErrorCodes
{
    extern const int ATTEMPT_TO_READ_AFTER_EOF;
}

namespace
{

void copyDataImpl(ReadBuffer & from, WriteBuffer & to, bool check_bytes, size_t bytes, const std::atomic<int> * is_cancelled)
{
    /// If read to the end of the buffer, eof() either fills the buffer with new data and moves the cursor to the beginning, or returns false.
    while (bytes > 0 && !from.eof())
    {
        if (is_cancelled && *is_cancelled)
            return;

        /// buffer() - a piece of data available for reading; position() - the cursor of the place to which you have already read.
        size_t count = std::min(bytes, static_cast<size_t>(from.buffer().end() - from.position()));
        to.write(from.position(), count);
        from.position() += count;
        bytes -= count;
    }

    if (check_bytes && bytes > 0)
        throw Exception("Attempt to read after EOF.", ErrorCodes::ATTEMPT_TO_READ_AFTER_EOF);
}

void copyDataImpl(ReadBuffer & from, WriteBuffer & to, bool check_bytes, size_t bytes, std::function<void()> cancellation_hook)
{
    /// If read to the end of the buffer, eof() either fills the buffer with new data and moves the cursor to the beginning, or returns false.
    while (bytes > 0 && !from.eof())
    {
        if (cancellation_hook)
            cancellation_hook();

        /// buffer() - a piece of data available for reading; position() - the cursor of the place to which you have already read.
        size_t count = std::min(bytes, static_cast<size_t>(from.buffer().end() - from.position()));
        to.write(from.position(), count);
        from.position() += count;
        bytes -= count;
    }

    if (check_bytes && bytes > 0)
        throw Exception("Attempt to read after EOF.", ErrorCodes::ATTEMPT_TO_READ_AFTER_EOF);
}

}

void copyData(ReadBuffer & from, WriteBuffer & to)
{
    copyDataImpl(from, to, false, std::numeric_limits<size_t>::max(), nullptr);
}

void copyData(ReadBuffer & from, WriteBuffer & to, const std::atomic<int> & is_cancelled)
{
    copyDataImpl(from, to, false, std::numeric_limits<size_t>::max(), &is_cancelled);
}

void copyData(ReadBuffer & from, WriteBuffer & to, std::function<void()> cancellation_hook)
{
    copyDataImpl(from, to, false, std::numeric_limits<size_t>::max(), cancellation_hook);
}

void copyData(ReadBuffer & from, WriteBuffer & to, size_t bytes)
{
    copyDataImpl(from, to, true, bytes, nullptr);
}

void copyData(ReadBuffer & from, WriteBuffer & to, size_t bytes, const std::atomic<int> & is_cancelled)
{
    copyDataImpl(from, to, true, bytes, &is_cancelled);
}

void copyData(ReadBuffer & from, WriteBuffer & to, size_t bytes, std::function<void()> cancellation_hook)
{
    copyDataImpl(from, to, true, bytes, cancellation_hook);
}

}
