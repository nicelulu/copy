#pragma once

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/URI.h>
#include <DB/IO/ReadBufferFromHTTP.h>
#include <DB/IO/ReadBuffer.h>

namespace DB
{

struct HTTPTimeouts {
	Poco::Timespan connection_timeout = Poco::Timespan(DEFAULT_HTTP_READ_BUFFER_CONNECTION_TIMEOUT, 0);
	Poco::Timespan send_timeout = Poco::Timespan(DEFAULT_HTTP_READ_BUFFER_TIMEOUT, 0);
	Poco::Timespan receive_timeout = Poco::Timespan(DEFAULT_HTTP_READ_BUFFER_TIMEOUT, 0);
};

/** Perform HTTP POST request and provide response to read.
  */
class ReadWriteBufferFromHTTP : public ReadBuffer
{
private:
	Poco::URI uri;
	std::string method;
	HTTPTimeouts timeouts;

	Poco::Net::HTTPClientSession session;
	std::istream * istr;	/// owned by session
	std::unique_ptr<ReadBuffer> impl;

public:
	ReadWriteBufferFromHTTP(
		const Poco::URI & uri,
		const std::string & method = {},
		const std::string & post_body = {},
		size_t buffer_size_ = DBMS_DEFAULT_BUFFER_SIZE,
		const HTTPTimeouts & timeouts = {}
	);

	bool nextImpl() override;

};

}
