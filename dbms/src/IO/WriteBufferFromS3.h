#pragma once

#include <functional>
#include <memory>
#include <Core/Types.h>
#include <IO/ConnectionTimeouts.h>
#include <IO/HTTPCommon.h>
#include <IO/BufferWithOwnMemory.h>
#include <IO/ReadBuffer.h>
#include <IO/ReadBufferFromIStream.h>
#include <IO/WriteBuffer.h>
#include <IO/WriteBufferFromString.h>
#include <Poco/Net/HTTPBasicCredentials.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/URI.h>
#include <Poco/Version.h>
#include <Common/DNSResolver.h>
#include <Common/config.h>
#include <common/logger_useful.h>


namespace DB
{
/* Perform S3 HTTP PUT request.
 */
class WriteBufferFromS3 : public BufferWithOwnMemory<WriteBuffer>
{
private:
    Poco::URI uri;
    ConnectionTimeouts timeouts;
    Poco::Net::HTTPRequest auth_request;
    String buffer_string;
    DB::WriteBufferFromString temporary_buffer;

public:
    explicit WriteBufferFromS3(const Poco::URI & uri,
        const ConnectionTimeouts & timeouts = {},
        const Poco::Net::HTTPBasicCredentials & credentials = {},
        size_t buffer_size_ = DBMS_DEFAULT_BUFFER_SIZE);

    void nextImpl() override;

    /// Receives response from the server after sending all data.
    void finalize();

    ~WriteBufferFromS3();
};

}
