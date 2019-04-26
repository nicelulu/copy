#pragma once

#include <atomic>
#include <cstddef>
#include <common/Types.h>

#include <Core/Defines.h>

namespace DB
{

class ReadBuffer;
class WriteBuffer;

struct AllProgressValueImpl;

/// See Progress.
struct ProgressValues
{
    size_t rows;
    size_t bytes;
    size_t total_rows;
    size_t write_rows;
    size_t write_bytes;   

    template <typename ReadImpl = AllProgressValueImpl>
    void read(ReadBuffer & in, UInt64 server_revision)
    {
        ReadImpl::read(*this, in, server_revision);
    }

    template <typename WriteImpl = AllProgressValueImpl>
    void write(WriteBuffer & out, UInt64 client_revision) const
    {
        WriteImpl::write(*this, out, client_revision);
    }

    template <typename WriteJSONImpl = AllProgressValueImpl>
    void writeJSON(WriteBuffer & out) const
    {
        WriteJSONImpl::writeJSON(*this, out);
    }
};

struct AllProgressValueImpl
{
    static void read(ProgressValues & value, ReadBuffer & in, UInt64 server_revision);
    static void write(const ProgressValues & value, WriteBuffer & out, UInt64 client_revision) ;
    static void writeJSON(const ProgressValues & value, WriteBuffer & out);
};

struct ReadProgressValueImpl : public AllProgressValueImpl
{
    static void writeJSON(const ProgressValues & value, WriteBuffer & out);
};

struct WriteProgressValueImpl : public AllProgressValueImpl
{
    static void writeJSON(const ProgressValues & value, WriteBuffer & out);
};

struct ReadProgress
{
    size_t rows;
    size_t bytes;
    size_t total_rows;

    ReadProgress(size_t rows_, size_t bytes_, size_t total_rows_ = 0)
        : rows(rows_), bytes(bytes_), total_rows(total_rows_) {}
};

struct WriteProgress
{
    size_t write_rows;
    size_t write_bytes;

    WriteProgress(size_t write_rows_, size_t write_bytes_)
        : write_rows(write_rows_), write_bytes(write_bytes_) {}
};

/** Progress of query execution.
  * Values, transferred over network are deltas - how much was done after previously sent value.
  * The same struct is also used for summarized values.
  */
struct Progress
{
    std::atomic<size_t> rows {0};        /// Rows (source) processed.
    std::atomic<size_t> bytes {0};       /// Bytes (uncompressed, source) processed.

    /** How much rows must be processed, in total, approximately. Non-zero value is sent when there is information about some new part of job.
      * Received values must be summed to get estimate of total rows to process.
      * Used for rendering progress bar on client.
      */
    std::atomic<size_t> total_rows {0};


    std::atomic<size_t> write_rows {0};
    std::atomic<size_t> write_bytes {0};

    Progress() {}
    Progress(size_t rows_, size_t bytes_, size_t total_rows_ = 0)
        : rows(rows_), bytes(bytes_), total_rows(total_rows_) {}
    Progress(ReadProgress read_progress)
        : rows(read_progress.rows), bytes(read_progress.bytes), total_rows(read_progress.total_rows) {}
    Progress(WriteProgress write_progress)
        : write_rows(write_progress.write_rows), write_bytes(write_progress.write_bytes)  {}

    template <typename T = AllProgressValueImpl>
    void read(ReadBuffer & in, UInt64 server_revision);

    template <typename T = AllProgressValueImpl>
    void write(WriteBuffer & out, UInt64 client_revision) const;

    /// Progress in JSON format (single line, without whitespaces) is used in HTTP headers.
    template <typename T = AllProgressValueImpl>
    void writeJSON(WriteBuffer & out) const;

    /// Each value separately is changed atomically (but not whole object).
    bool incrementPiecewiseAtomically(const Progress & rhs)
    {
        rows += rhs.rows;
        bytes += rhs.bytes;
        total_rows += rhs.total_rows;
        write_rows += rhs.write_rows;
        write_bytes += rhs.write_bytes;

        return rhs.rows || rhs.write_rows ? true : false;
    }

    void reset()
    {
        rows = 0;
        bytes = 0;
        total_rows = 0;
        write_rows = 0;
        write_bytes = 0;
    }

    ProgressValues getValues() const
    {
        ProgressValues res;

        res.rows = rows.load(std::memory_order_relaxed);
        res.bytes = bytes.load(std::memory_order_relaxed);
        res.total_rows = total_rows.load(std::memory_order_relaxed);
        res.write_rows = write_rows.load(std::memory_order_relaxed);
        res.write_bytes = write_bytes.load(std::memory_order_relaxed);

        return res;
    }

    ProgressValues fetchAndResetPiecewiseAtomically()
    {
        ProgressValues res;

        res.rows = rows.fetch_and(0);
        res.bytes = bytes.fetch_and(0);
        res.total_rows = total_rows.fetch_and(0);
        res.write_rows = write_rows.fetch_and(0);
        res.write_bytes = write_bytes.fetch_and(0);
 
        return res;
    }

    Progress & operator=(Progress && other)
    {
        rows = other.rows.load(std::memory_order_relaxed);
        bytes = other.bytes.load(std::memory_order_relaxed);
        total_rows = other.total_rows.load(std::memory_order_relaxed);
        write_rows = other.write_rows.load(std::memory_order_relaxed);
        write_bytes = other.write_bytes.load(std::memory_order_relaxed);
 
        return *this;
    }

    Progress(Progress && other)
    {
        *this = std::move(other);
    }
};

template <typename T>
void Progress::read(ReadBuffer & in, UInt64 server_revision)
{
    ProgressValues values;
    values.read<T>(in, server_revision);

    rows.store(values.rows, std::memory_order_relaxed);
    bytes.store(values.bytes, std::memory_order_relaxed);
    total_rows.store(values.total_rows, std::memory_order_relaxed);
    write_rows.store(values.write_rows, std::memory_order_relaxed);
    write_bytes.store(values.write_bytes, std::memory_order_relaxed);
}

template <typename T>
void Progress::write(WriteBuffer & out, UInt64 client_revision) const
{
    getValues().write<T>(out, client_revision);
}


template <typename T>
void Progress::writeJSON(WriteBuffer & out) const
{
    getValues().writeJSON<T>(out);
}


}
