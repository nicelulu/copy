#include "ServiceTCPHandler.h"
#include <chrono>
#include <mutex>
#include <queue>
#include <Core/Types.h>
#include <IO/ReadBufferFromFileDescriptor.h>
#include <IO/ReadBufferFromPocoSocket.h>
#include <IO/WriteBufferFromPocoSocket.h>
#include <Service/FourLetterCommand.h>
#include <Service/SvsKeeperProfileEvents.h>
#include <Poco/Net/NetException.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <Common/CurrentThread.h>
#include <Common/NetException.h>
#include <Common/PipeFDs.h>
#include <Common/Stopwatch.h>
#include <Common/ZooKeeper/ZooKeeperIO.h>
#include <Common/setThreadName.h>
#include <common/logger_useful.h>

#ifdef POCO_HAVE_FD_EPOLL
#    include <sys/epoll.h>
#else
#    include <poll.h>
#endif

namespace ServiceProfileEvents
{
extern const Event req_time;
}

namespace DB
{
namespace ErrorCodes
{
    extern const int SYSTEM_ERROR;
    extern const int LOGICAL_ERROR;
    extern const int UNEXPECTED_PACKET_FROM_CLIENT;
    extern const int TIMEOUT_EXCEEDED;
}

struct PollResult
{
    bool has_requests{false};
    bool error{false};
};

struct SocketInterruptablePollWrapper
{
    int sockfd;

#if defined(POCO_HAVE_FD_EPOLL)
    int epollfd;
    epoll_event socket_event{};
    epoll_event pipe_event{};
#endif

    using InterruptCallback = std::function<void()>;

    explicit SocketInterruptablePollWrapper(const Poco::Net::StreamSocket & poco_socket_)
        : sockfd(poco_socket_.impl()->sockfd())
    {
#if defined(POCO_HAVE_FD_EPOLL)
        epollfd = epoll_create(1);
        if (epollfd < 0)
            throwFromErrno("Cannot epoll_create", ErrorCodes::SYSTEM_ERROR);

        socket_event.events = EPOLLIN | EPOLLERR | EPOLLPRI;
        socket_event.data.fd = sockfd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &socket_event) < 0)
        {
            ::close(epollfd);
            throwFromErrno("Cannot insert socket into epoll queue", ErrorCodes::SYSTEM_ERROR);
        }
#endif
    }

    PollResult poll(Poco::Timespan remaining_time, const std::shared_ptr<ReadBufferFromPocoSocket> & in)
    {
        bool socket_ready = false;

        if (in->available() != 0)
            socket_ready = true;

        int rc = 0;
        if (!socket_ready)
        {
#if defined(POCO_HAVE_FD_EPOLL)
            epoll_event evout[1];
            evout[0].data.fd = -1;
            do
            {
                Poco::Timestamp start;
                rc = epoll_wait(epollfd, evout, 1, remaining_time.totalMilliseconds());
                if (rc < 0 && errno == EINTR)
                {
                    Poco::Timestamp end;
                    Poco::Timespan waited = end - start;
                    if (waited < remaining_time)
                        remaining_time -= waited;
                    else
                        remaining_time = 0;
                }
            } while (rc < 0 && errno == EINTR);

            if (rc >= 1)
                socket_ready = true;
#else
            pollfd poll_buf[1];
            poll_buf[0].fd = sockfd;
            poll_buf[0].events = POLLIN;

            do
            {
                Poco::Timestamp start;
                rc = ::poll(poll_buf, 1, remaining_time.totalMilliseconds());
                if (rc < 0 && errno == POCO_EINTR)
                {
                    Poco::Timestamp end;
                    Poco::Timespan waited = end - start;
                    if (waited < remaining_time)
                        remaining_time -= waited;
                    else
                        remaining_time = 0;
                }
            } while (rc < 0 && errno == POCO_EINTR);

            if (rc >= 1 && poll_buf[0].revents & POLLIN)
                socket_ready = true;
#endif
        }

        PollResult result{};
        result.has_requests = socket_ready;

        if (rc < 0)
            result.error = true;

        return result;
    }

#if defined(POCO_HAVE_FD_EPOLL)
    ~SocketInterruptablePollWrapper() { ::close(epollfd); }
#endif
};

ServiceTCPHandler::ServiceTCPHandler(IServer & server_, const Poco::Net::StreamSocket & socket_)
    : Poco::Net::TCPServerConnection(socket_)
    , server(server_)
    , log(&Poco::Logger::get("ServiceTCPHandler"))
    , global_context(server.context())
    , service_keeper_storage_dispatcher(global_context.getSvsKeeperStorageDispatcher())
    , operation_timeout(
          0, global_context.getConfigRef().getUInt("service.coordination_settings.operation_timeout_ms", Coordination::DEFAULT_OPERATION_TIMEOUT_MS) * 1000)
    , session_timeout(
          0, global_context.getConfigRef().getUInt("service.coordination_settings.session_timeout_ms", Coordination::DEFAULT_SESSION_TIMEOUT_MS) * 1000)
    , poll_wrapper(std::make_unique<SocketInterruptablePollWrapper>(socket_))
    , responses(std::make_unique<ThreadSafeResponseQueue>())
{
}

void ServiceTCPHandler::sendHandshake(bool has_leader)
{
    Coordination::write(Coordination::SERVER_HANDSHAKE_LENGTH, *out);
    if (has_leader)
        Coordination::write(Coordination::ZOOKEEPER_PROTOCOL_VERSION, *out);
    else /// Specially ignore connections if we are not leader, client will throw exception
        Coordination::write(42, *out);

    Coordination::write(static_cast<int32_t>(session_timeout.totalMilliseconds()), *out);
    Coordination::write(session_id, *out);
    std::array<char, Coordination::PASSWORD_LENGTH> passwd{};
    Coordination::write(passwd, *out);
    out->next();
}

void ServiceTCPHandler::run()
{
    send_thread = ThreadFromGlobalPool(&ServiceTCPHandler::sendThread, this);
    runImpl();
}

Poco::Timespan ServiceTCPHandler::receiveHandshake(int32_t handshake_length)
{
    /// for letter admin commands
    int32_t protocol_version;
    int64_t last_zxid_seen;
    int32_t timeout_ms;
    int64_t previous_session_id = 0; /// We don't support session restore. So previous session_id is always zero.
    std::array<char, Coordination::PASSWORD_LENGTH> passwd{};
    if (!isHandShake(handshake_length))
        throw Exception("Unexpected handshake length received: " + toString(handshake_length), ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(protocol_version, *in);

    if (protocol_version != Coordination::ZOOKEEPER_PROTOCOL_VERSION)
        throw Exception("Unexpected protocol version: " + toString(protocol_version), ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(last_zxid_seen, *in);

    if (last_zxid_seen != 0)
        throw Exception("Client Last zxid seen is " + toString(last_zxid_seen) + ", non zero last_zxid_seen is not supported", ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(timeout_ms, *in);
    Coordination::read(previous_session_id, *in);

    if (previous_session_id != 0 && previous_session_id != -1)
        throw Exception("Previous session id is " + toString(previous_session_id) + ", non zero and -1 previous session id is not supported", ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(passwd, *in);

    int8_t readonly;
    if (handshake_length == Coordination::CLIENT_HANDSHAKE_LENGTH_WITH_READONLY)
        Coordination::read(readonly, *in);

    return Poco::Timespan(0, timeout_ms * 1000);
}

bool ServiceTCPHandler::isHandShake(Int32 & handshake_length)
{
    return handshake_length == Coordination::CLIENT_HANDSHAKE_LENGTH
        || handshake_length == Coordination::CLIENT_HANDSHAKE_LENGTH_WITH_READONLY;
}

bool ServiceTCPHandler::tryExecuteFourLetterWordCmd(Int32 & four_letter_cmd)
{
    if(FourLetterCommands::isKnown(four_letter_cmd))
    {
        auto command = FourLetterCommands::getCommand(four_letter_cmd);
        LOG_DEBUG(log, "receive four letter command {}", command->name());

        String res;
        try
        {
            command->run(res);
        }
        catch (const Exception & e)
        {
            res = "Error when executing four letter command " + command->name() + ". Because: " + e.displayText();
            tryLogCurrentException(log, res);
        }

        try
        {
            out->write(res.data(), res.size());
        }
        catch (const Exception &)
        {
            tryLogCurrentException(log, "Error when send 4 letter command response");
        }

        return true;
    }
    else
    {
        LOG_WARNING(log, "invalid four letter command {}", std::to_string(four_letter_cmd));
    }
    return false;
}

void ServiceTCPHandler::runImpl()
{
    setThreadName("SvsKeeprHandler");
    ThreadStatus thread_status;
    auto global_receive_timeout = global_context.getSettingsRef().receive_timeout;
    auto global_send_timeout = global_context.getSettingsRef().send_timeout;

    socket().setReceiveTimeout(global_receive_timeout);
    socket().setSendTimeout(global_send_timeout);
    socket().setNoDelay(true);

    in = std::make_shared<ReadBufferFromPocoSocket>(socket());
    out = std::make_shared<WriteBufferFromPocoSocket>(socket());

    if (in->eof())
    {
        LOG_WARNING(log, "Client has not sent any data.");
        return;
    }

    int32_t header;
    try
    {
        Coordination::read(header, *in);
    }
    catch(const Exception & e)
    {
        LOG_WARNING(log, "Error while read connection header {}", e.displayText());
        return;
    }

    /// All four letter word command code is larger than 2^24 or lower than 0.
    /// Hand shake package length must be lower than 2^24 and larger than 0.
    /// So collision never happens.
    int32_t four_letter_cmd = header;
    if(!isHandShake(four_letter_cmd))
    {
        tryExecuteFourLetterWordCmd(four_letter_cmd);
        return;
    }

    try
    {
        LOG_TRACE(log, "Server session_timeout is {}.", session_timeout.milliseconds());

        int32_t handshake_length = header;
        auto client_timeout = receiveHandshake(handshake_length);

        LOG_TRACE(log, "ReceiveHandshake client session_timeout is {}.", client_timeout.milliseconds());

        if (client_timeout != 0)
            session_timeout = std::min(client_timeout, session_timeout);
    }
    catch (const Exception & e) /// Typical for an incorrect username, password, or address.
    {
        LOG_WARNING(log, "Cannot receive handshake {}", e.displayText());
        return;
    }

    if (service_keeper_storage_dispatcher->hasLeader())
    {
        try
        {
            LOG_INFO(log, "Requesting session ID for the new client");
            session_id = service_keeper_storage_dispatcher->getSessionID(session_timeout.totalMilliseconds());
            LOG_INFO(log, "Received session ID {}", session_id);
        }
        catch (const Exception & e)
        {
            LOG_WARNING(log, "Cannot receive session id {}", e.displayText());
            sendHandshake(false);
            return;
        }

        sendHandshake(true);
    }
    else
    {
        LOG_WARNING(log, "Ignoring user request, because no alive leader exist");
        sendHandshake(false);
        return;
    }

    auto response_callback = [this](const Coordination::ZooKeeperResponsePtr & response) {
        responses->push(response);
    };
    service_keeper_storage_dispatcher->registerSession(session_id, response_callback);

    session_stopwatch.start();
    bool close_received = false;

    try
    {
        while (!closed)
        {
            using namespace std::chrono_literals;

            PollResult result = poll_wrapper->poll(session_timeout, in);
            if (result.has_requests && !close_received)
            {
                receiveRequest();
                /// Each request restarts session stopwatch
                session_stopwatch.restart();
            }

            if (result.error)
                throw Exception("Exception happened while reading from socket", ErrorCodes::SYSTEM_ERROR);

            if (session_stopwatch.elapsedMicroseconds() > static_cast<UInt64>(session_timeout.totalMicroseconds()))
            {
                LOG_DEBUG(log, "Session #{} expired", session_id);
                service_keeper_storage_dispatcher->finishSession(session_id);
                break;
            }
        }
    }
    catch (const Exception & ex)
    {
        closed = true;
        LOG_INFO(log, "Got exception processing session #{}: {}", session_id, getExceptionMessage(ex, true));
        service_keeper_storage_dispatcher->finishSession(session_id);
    }
}

void ServiceTCPHandler::sendThread()
{
    setThreadName("SvsKeeprSender");
    try
    {
        while (!closed)
        {
            Coordination::ZooKeeperResponsePtr response;

            if (!responses->tryPop(response, session_timeout.totalMilliseconds()))
            {
                closed = true;
                LOG_DEBUG(log, "Session #{} expired.", session_id);
                return;
            }

            if (response->xid == close_xid)
            {
                closed = true;
                LOG_DEBUG(log, "Session #{} successfully closed", session_id);
                return;
            }

            LOG_DEBUG(log, "Send response session {}, xid {}, zxid {}, error {}", session_id, response->xid, response->zxid, response->error);
            response->write(*out);

            if (response->xid == Coordination::PING_XID)
            {
                LOG_TRACE(log, "Send heartbeat for session #{}", session_id);
            }
            if (response->error == Coordination::Error::ZSESSIONEXPIRED)
            {
                closed = true;
                LOG_DEBUG(log, "Session #{} expired because server shutting down or quorum is not alive", session_id);
                service_keeper_storage_dispatcher->finishSession(session_id);
                return;
            }
        }
    }
    catch (const Exception & ex)
    {
        closed = true;
        LOG_INFO(log, "Got exception processing session #{}: {}", session_id, getExceptionMessage(ex, true));
        service_keeper_storage_dispatcher->finishSession(session_id);
    }
}

std::pair<Coordination::OpNum, Coordination::XID> ServiceTCPHandler::receiveRequest()
{
    int32_t length;
    Coordination::read(length, *in);

    int32_t xid;
    Coordination::read(xid, *in);

    Coordination::OpNum opnum;
    Coordination::read(opnum, *in);

    LOG_INFO(log, "Receive request session {}, xid {}, length {}, opnum {}", session_id, xid, length, opnum);

    Coordination::ZooKeeperRequestPtr request = Coordination::ZooKeeperRequestFactory::instance().get(opnum);
    request->xid = xid;
    request->readImpl(*in);

    if (request->isReadRequest())
    {
        SvsKeeperStorage::RequestForSession request_info;
        request_info.request = request;
        request_info.session_id = session_id;
        const auto & read_responses = service_keeper_storage_dispatcher->singleProcessReadRequest(request_info);
        for (const auto & session_response : read_responses)
        {
            session_response.response->write(*out);
        }
    }
    else
    {
        if (!service_keeper_storage_dispatcher->putRequest(request, session_id))
            throw Exception(ErrorCodes::TIMEOUT_EXCEEDED, "Session {} already disconnected", session_id);
    }
    return std::make_pair(opnum, xid);
}

ServiceTCPHandler::~ServiceTCPHandler()
{
    closed = true;
    if (send_thread.joinable())
        send_thread.join();
}

}
