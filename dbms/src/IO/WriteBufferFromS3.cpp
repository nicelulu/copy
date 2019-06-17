#include <IO/WriteBufferFromS3.h>

#include <IO/WriteHelpers.h>

#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/NodeList.h>
#include <Poco/SAX/InputSource.h>

#include <common/logger_useful.h>


#define DEFAULT_S3_MAX_FOLLOW_PUT_REDIRECT 2
#define DEFAULT_S3_MINIMUM_PART_SIZE 100'000'000

namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_DATA;
}


WriteBufferFromS3::WriteBufferFromS3(
    const Poco::URI & uri_, const ConnectionTimeouts & timeouts_, 
    const Poco::Net::HTTPBasicCredentials & credentials, size_t buffer_size_)
    : BufferWithOwnMemory<WriteBuffer>(buffer_size_, nullptr, 0)
    , uri {uri_}
    , timeouts {timeouts_}
    , auth_request {Poco::Net::HTTPRequest::HTTP_PUT, uri.getPathAndQuery(), Poco::Net::HTTPRequest::HTTP_1_1}
    , temporary_buffer {std::make_unique<WriteBufferFromString>(buffer_string)}
    , part_number {1}
    , last_part_size {0}
{
    if (!credentials.getUsername().empty())
        credentials.authenticate(auth_request);

    initiate();
}


void WriteBufferFromS3::nextImpl()
{
    if (!offset())
        return;

    temporary_buffer->write(working_buffer.begin(), offset());

    last_part_size += offset();

    if (last_part_size > DEFAULT_S3_MINIMUM_PART_SIZE)
    {
        temporary_buffer->finish();
        writePart(buffer_string);
        last_part_size = 0;
        temporary_buffer = std::make_unique<WriteBufferFromString>(buffer_string);
    }
}


void WriteBufferFromS3::finalize()
{
    temporary_buffer->finish();
    if (!buffer_string.empty())
    {
        writePart(buffer_string);
        ++part_number;
    }

    complete();
}


WriteBufferFromS3::~WriteBufferFromS3()
{
    try
    {
        next();
    }
    catch (...)
    {
        tryLogCurrentException(__PRETTY_FUNCTION__);
    }
}


void WriteBufferFromS3::initiate()
{
    // https://docs.aws.amazon.com/AmazonS3/latest/API/mpUploadInitiate.html
    Poco::Net::HTTPResponse response;
    std::unique_ptr<Poco::Net::HTTPRequest> request;
    HTTPSessionPtr session;
    std::istream * istr = nullptr; /// owned by session
    Poco::URI initiate_uri = uri;
    initiate_uri.setRawQuery("uploads"); // FIXME find how to leave user params as is

    for (int i = 0; i < DEFAULT_S3_MAX_FOLLOW_PUT_REDIRECT; ++i)
    {
        session = makeHTTPSession(initiate_uri, timeouts);
        request = std::make_unique<Poco::Net::HTTPRequest>(Poco::Net::HTTPRequest::HTTP_POST, initiate_uri.getPathAndQuery(), Poco::Net::HTTPRequest::HTTP_1_1);
        request->setHost(initiate_uri.getHost()); // use original, not resolved host name in header

        if (auth_request.hasCredentials()) {
            Poco::Net::HTTPBasicCredentials credentials(auth_request);
            credentials.authenticate(*request);
        }

        request->setContentLength(0);

        LOG_TRACE((&Logger::get("WriteBufferFromS3")), "Sending request to " << initiate_uri.toString());

        session->sendRequest(*request);

        istr = &session->receiveResponse(response);

        if (response.getStatus() != 307)
            break;

        auto location_iterator = response.find("Location");
        if (location_iterator == response.end())
            break;

        initiate_uri = location_iterator->second;
    }
    assertResponseIsOk(*request, response, *istr);

    Poco::XML::InputSource src(*istr);
    Poco::XML::DOMParser parser;
    Poco::AutoPtr<Poco::XML::Document> document = parser.parse(&src);
    Poco::AutoPtr<Poco::XML::NodeList> nodes = document->getElementsByTagName("UploadId");
    if (nodes->length() != 1)
    {
        throw Exception("Incorrect XML in response, no upload id", ErrorCodes::INCORRECT_DATA);
    }
    upload_id = nodes->item(0)->innerText();
    if (upload_id.empty())
    {
        throw Exception("Incorrect XML in response, empty upload id", ErrorCodes::INCORRECT_DATA);
    }
}
 

void WriteBufferFromS3::writePart(const String & data)
{
    // https://docs.aws.amazon.com/AmazonS3/latest/API/mpUploadUploadPart.html
    Poco::Net::HTTPResponse response;
    std::unique_ptr<Poco::Net::HTTPRequest> request;
    HTTPSessionPtr session;
    std::istream * istr = nullptr; /// owned by session
    Poco::URI part_uri = uri;
    part_uri.addQueryParameter("partNumber", std::to_string(part_number));
    part_uri.addQueryParameter("uploadId", upload_id);

    for (int i = 0; i < DEFAULT_S3_MAX_FOLLOW_PUT_REDIRECT; ++i)
    {
        session = makeHTTPSession(part_uri, timeouts);
        request = std::make_unique<Poco::Net::HTTPRequest>(Poco::Net::HTTPRequest::HTTP_PUT, part_uri.getPathAndQuery(), Poco::Net::HTTPRequest::HTTP_1_1);
        request->setHost(part_uri.getHost()); // use original, not resolved host name in header

        if (auth_request.hasCredentials()) {
            Poco::Net::HTTPBasicCredentials credentials(auth_request);
            credentials.authenticate(*request);
        }

        request->setExpectContinue(true);

        request->setContentLength(data.size());

        LOG_TRACE((&Logger::get("WriteBufferFromS3")), "Sending request to " << part_uri.toString());

        std::ostream & ostr = session->sendRequest(*request);
//        if (session->peekResponse(response)) // FIXME, shall not go next if not received 100-continue
        {
            // Received 100-continue.
            ostr << data;
        }

        istr = &session->receiveResponse(response);

        if (response.getStatus() != 307)
            break;

        auto location_iterator = response.find("Location");
        if (location_iterator == response.end())
            break;

        part_uri = location_iterator->second;
    }
    assertResponseIsOk(*request, response, *istr);

    auto etag_iterator = response.find("ETag");
    if (etag_iterator == response.end())
    {
        throw Exception("Incorrect response, no ETag", ErrorCodes::INCORRECT_DATA);
    }
    part_tags.push_back(etag_iterator->second);
}


void WriteBufferFromS3::complete()
{
    // https://docs.aws.amazon.com/AmazonS3/latest/API/mpUploadComplete.html
    Poco::Net::HTTPResponse response;
    std::unique_ptr<Poco::Net::HTTPRequest> request;
    HTTPSessionPtr session;
    std::istream * istr = nullptr; /// owned by session
    Poco::URI complete_uri = uri;
    complete_uri.addQueryParameter("uploadId", upload_id);

    String data;
    WriteBufferFromString buffer(data);
    writeString("<CompleteMultipartUpload>", buffer); // FIXME move to Poco::XML maybe??
    for (size_t i = 0; i < part_tags.size(); ++i) {
        writeString("<Part><PartNumber>", buffer);
        writeIntText(i + 1, buffer);
        writeString("</PartNumber><ETag>", buffer);
        writeString(part_tags[i], buffer);
        writeString("</ETag></Part>", buffer);
    }
    writeString("</CompleteMultipartUpload>", buffer);
    buffer.finish();

    for (int i = 0; i < DEFAULT_S3_MAX_FOLLOW_PUT_REDIRECT; ++i)
    {
        session = makeHTTPSession(complete_uri, timeouts);
        request = std::make_unique<Poco::Net::HTTPRequest>(Poco::Net::HTTPRequest::HTTP_POST, complete_uri.getPathAndQuery(), Poco::Net::HTTPRequest::HTTP_1_1);
        request->setHost(complete_uri.getHost()); // use original, not resolved host name in header

        if (auth_request.hasCredentials()) {
            Poco::Net::HTTPBasicCredentials credentials(auth_request);
            credentials.authenticate(*request);
        }

        request->setExpectContinue(true);

        request->setContentLength(data.size());

        LOG_TRACE((&Logger::get("WriteBufferFromS3")), "Sending request to " << complete_uri.toString());

        std::ostream & ostr = session->sendRequest(*request);
//        if (session->peekResponse(response)) // FIXME, shall not go next if not received 100-continue
        {
            // Received 100-continue.
            ostr << data;
        }

        istr = &session->receiveResponse(response);

        if (response.getStatus() != 307)
            break;

        auto location_iterator = response.find("Location");
        if (location_iterator == response.end())
            break;

        complete_uri = location_iterator->second;
    }
    assertResponseIsOk(*request, response, *istr);
}

}
