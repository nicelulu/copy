#include <Server/NuKeeperTCPHandler.h>

#if USE_NURAFT

#include <Common/ZooKeeper/ZooKeeperIO.h>
#include <Core/Types.h>
#include <IO/WriteBufferFromPocoSocket.h>
#include <IO/ReadBufferFromPocoSocket.h>
#include <Poco/Net/NetException.h>
#include <Common/CurrentThread.h>
#include <Common/Stopwatch.h>
#include <Common/NetException.h>
#include <Common/setThreadName.h>
#include <common/logger_useful.h>
#include <chrono>
#include <Common/PipeFDs.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <IO/ReadBufferFromFileDescriptor.h>
#include <queue>
#include <mutex>

#ifdef POCO_HAVE_FD_EPOLL
    #include <sys/epoll.h>
#else
    #include <poll.h>
#endif


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
    size_t ready_responses_count{0};
    bool has_requests{false};
    bool error{false};
};

/// Queue with mutex. As simple as possible.
class ThreadSafeResponseQueue
{
private:
    mutable std::mutex queue_mutex;
    std::queue<Coordination::ZooKeeperResponsePtr> queue;
public:
    void push(const Coordination::ZooKeeperResponsePtr & response)
    {
        std::lock_guard lock(queue_mutex);
        queue.push(response);
    }
    bool tryPop(Coordination::ZooKeeperResponsePtr & response)
    {
        std::lock_guard lock(queue_mutex);
        if (!queue.empty())
        {
            response = queue.front();
            queue.pop();
            return true;
        }
        return false;
    }
    size_t size() const
    {
        std::lock_guard lock(queue_mutex);
        return queue.size();
    }
};

struct SocketInterruptablePollWrapper
{
    int sockfd;
    PipeFDs pipe;
    ReadBufferFromFileDescriptor response_in;

#if defined(POCO_HAVE_FD_EPOLL)
    int epollfd;
    epoll_event socket_event{};
    epoll_event pipe_event{};
#endif

    using InterruptCallback = std::function<void()>;

    explicit SocketInterruptablePollWrapper(const Poco::Net::StreamSocket & poco_socket_)
        : sockfd(poco_socket_.impl()->sockfd())
        , response_in(pipe.fds_rw[0])
    {
        pipe.setNonBlockingReadWrite();

#if defined(POCO_HAVE_FD_EPOLL)
        epollfd = epoll_create(2);
        if (epollfd < 0)
            throwFromErrno("Cannot epoll_create", ErrorCodes::SYSTEM_ERROR);

        socket_event.events = EPOLLIN | EPOLLERR;
        socket_event.data.fd = sockfd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &socket_event) < 0)
        {
            ::close(epollfd);
            throwFromErrno("Cannot insert socket into epoll queue", ErrorCodes::SYSTEM_ERROR);
        }
        pipe_event.events = EPOLLIN | EPOLLERR;
        pipe_event.data.fd = pipe.fds_rw[0];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, pipe.fds_rw[0], &pipe_event) < 0)
        {
            ::close(epollfd);
            throwFromErrno("Cannot insert socket into epoll queue", ErrorCodes::SYSTEM_ERROR);
        }
#endif
    }

    int getResponseFD() const
    {
        return pipe.fds_rw[1];
    }

    PollResult poll(Poco::Timespan remaining_time)
    {
        std::array<int, 2> outputs = {-1, -1};
#if defined(POCO_HAVE_FD_EPOLL)
        int rc;
        epoll_event evout[2];
        memset(evout, 0, sizeof(evout));
        do
        {
            Poco::Timestamp start;
            rc = epoll_wait(epollfd, evout, 2, remaining_time.totalMilliseconds());
            if (rc < 0 && errno == EINTR)
            {
                Poco::Timestamp end;
                Poco::Timespan waited = end - start;
                if (waited < remaining_time)
                    remaining_time -= waited;
                else
                    remaining_time = 0;
            }
        }
        while (rc < 0 && errno == EINTR);

        if (rc >= 1 && evout[0].events & EPOLLIN)
            outputs[0] = evout[0].data.fd;
        if (rc == 2 && evout[1].events & EPOLLIN)
            outputs[1] = evout[1].data.fd;
#else
        pollfd poll_buf[2];
        poll_buf[0].fd = sockfd;
        poll_buf[0].events = POLLIN;
        poll_buf[1].fd = pipe.fds_rw[0];
        poll_buf[1].events = POLLIN;

        int rc;
        do
        {
            Poco::Timestamp start;
            rc = ::poll(poll_buf, 2, remaining_time.totalMilliseconds());
            if (rc < 0 && errno == POCO_EINTR)
            {
                Poco::Timestamp end;
                Poco::Timespan waited = end - start;
                if (waited < remaining_time)
                    remaining_time -= waited;
                else
                    remaining_time = 0;
            }
        }
        while (rc < 0 && errno == POCO_EINTR);
        if (rc >= 1 && poll_buf[0].revents & POLLIN)
            outputs[0] = sockfd;
        if (rc == 2 && poll_buf[1].revents & POLLIN)
            outputs[1] = pipe.fds_rw[0];
#endif

        PollResult result{};
        if (rc < 0)
        {
            result.error = true;
            return result;
        }
        else if (rc == 0)
        {
            return result;
        }
        else
        {
            for (auto fd : outputs)
            {
                if (fd != -1)
                {
                    if (fd == sockfd)
                        result.has_requests = true;
                    else
                    {
                        UInt8 dummy;
                        do
                        {
                            /// All ready responses stored in responses queue,
                            /// but we have to count amount of ready responses in pipe
                            /// and process them only. Otherwise states of response_in
                            /// and response queue will be inconsistent and race condition is possible.
                            readIntBinary(dummy, response_in);
                            result.ready_responses_count++;
                        }
                        while (response_in.available());
                    }
                }
            }
        }
        return result;
    }

#if defined(POCO_HAVE_FD_EPOLL)
    ~SocketInterruptablePollWrapper()
    {
        ::close(epollfd);
    }
#endif
};

NuKeeperTCPHandler::NuKeeperTCPHandler(IServer & server_, const Poco::Net::StreamSocket & socket_)
    : Poco::Net::TCPServerConnection(socket_)
    , server(server_)
    , log(&Poco::Logger::get("NuKeeperTCPHandler"))
    , global_context(server.context())
    , nu_keeper_storage_dispatcher(global_context.getNuKeeperStorageDispatcher())
    , operation_timeout(0, global_context.getConfigRef().getUInt("test_keeper_server.operation_timeout_ms", Coordination::DEFAULT_OPERATION_TIMEOUT_MS) * 1000)
    , session_timeout(0, global_context.getConfigRef().getUInt("test_keeper_server.session_timeout_ms", Coordination::DEFAULT_SESSION_TIMEOUT_MS) * 1000)
    , poll_wrapper(std::make_unique<SocketInterruptablePollWrapper>(socket_))
    , responses(std::make_unique<ThreadSafeResponseQueue>())
{
}

void NuKeeperTCPHandler::sendHandshake(bool has_leader)
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

void NuKeeperTCPHandler::run()
{
    runImpl();
}

Poco::Timespan NuKeeperTCPHandler::receiveHandshake()
{
    int32_t handshake_length;
    int32_t protocol_version;
    int64_t last_zxid_seen;
    int32_t timeout_ms;
    int64_t previous_session_id = 0;    /// We don't support session restore. So previous session_id is always zero.
    std::array<char, Coordination::PASSWORD_LENGTH> passwd {};
    Coordination::read(handshake_length, *in);
    if (handshake_length != Coordination::CLIENT_HANDSHAKE_LENGTH && handshake_length != Coordination::CLIENT_HANDSHAKE_LENGTH_WITH_READONLY)
        throw Exception("Unexpected handshake length received: " + toString(handshake_length), ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(protocol_version, *in);

    if (protocol_version != Coordination::ZOOKEEPER_PROTOCOL_VERSION)
        throw Exception("Unexpected protocol version: " + toString(protocol_version), ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(last_zxid_seen, *in);

    if (last_zxid_seen != 0)
        throw Exception("Non zero last_zxid_seen is not supported", ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(timeout_ms, *in);
    Coordination::read(previous_session_id, *in);

    if (previous_session_id != 0)
        throw Exception("Non zero previous session id is not supported", ErrorCodes::UNEXPECTED_PACKET_FROM_CLIENT);

    Coordination::read(passwd, *in);

    int8_t readonly;
    if (handshake_length == Coordination::CLIENT_HANDSHAKE_LENGTH_WITH_READONLY)
        Coordination::read(readonly, *in);

    return Poco::Timespan(0, timeout_ms * 1000);
}


void NuKeeperTCPHandler::runImpl()
{
    setThreadName("TstKprHandler");
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

    try
    {
        auto client_timeout = receiveHandshake();
        if (client_timeout != 0)
            session_timeout = std::min(client_timeout, session_timeout);
    }
    catch (const Exception & e) /// Typical for an incorrect username, password, or address.
    {
        LOG_WARNING(log, "Cannot receive handshake {}", e.displayText());
        return;
    }

    if (nu_keeper_storage_dispatcher->hasLeader())
    {
        try
        {
            session_id = nu_keeper_storage_dispatcher->getSessionID(session_timeout.totalMilliseconds());
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

    auto response_fd = poll_wrapper->getResponseFD();
    auto response_callback = [this, response_fd] (const Coordination::ZooKeeperResponsePtr & response)
    {
        responses->push(response);
        UInt8 single_byte = 1;
        [[maybe_unused]] int result = write(response_fd, &single_byte, sizeof(single_byte));
    };
    nu_keeper_storage_dispatcher->registerSession(session_id, response_callback);

    session_stopwatch.start();
    bool close_received = false;
    try
    {
        while (true)
        {
            using namespace std::chrono_literals;

            PollResult result = poll_wrapper->poll(session_timeout);
            if (result.has_requests && !close_received)
            {
                do
                {
                    auto [received_op, received_xid] = receiveRequest();

                    if (received_op == Coordination::OpNum::Close)
                    {
                        LOG_DEBUG(log, "Received close event with xid {} for session id #{}", received_xid, session_id);
                        close_xid = received_xid;
                        close_received = true;
                        break;
                    }
                    else if (received_op == Coordination::OpNum::Heartbeat)
                    {
                        LOG_TRACE(log, "Received heartbeat for session #{}", session_id);
                        session_stopwatch.restart();
                    }
                }
                while (in->available());
            }

            /// Process exact amount of responses from pipe
            /// otherwise state of responses queue and signaling pipe
            /// became inconsistent and race condition is possible.
            while (result.ready_responses_count != 0)
            {
                Coordination::ZooKeeperResponsePtr response;
                if (!responses->tryPop(response))
                    throw Exception(ErrorCodes::LOGICAL_ERROR, "We must have at least {} ready responses, but queue is empty. It's a bug.", result.ready_responses_count);

                if (response->xid == close_xid)
                {
                    LOG_DEBUG(log, "Session #{} successfully closed", session_id);
                    return;
                }

                if (response->error == Coordination::Error::ZOK)
                    response->write(*out);
                else if (response->xid != Coordination::WATCH_XID)
                    response->write(*out);
                /// skipping bad response for watch
                result.ready_responses_count--;
            }

            if (result.error)
                throw Exception("Exception happened while reading from socket", ErrorCodes::SYSTEM_ERROR);

            if (session_stopwatch.elapsedMicroseconds() > static_cast<UInt64>(session_timeout.totalMicroseconds()))
            {
                LOG_DEBUG(log, "Session #{} expired", session_id);
                nu_keeper_storage_dispatcher->finishSession(session_id);
                break;
            }
        }
    }
    catch (const Exception & ex)
    {
        LOG_INFO(log, "Got exception processing session #{}: {}", session_id, getExceptionMessage(ex, true));
        nu_keeper_storage_dispatcher->finishSession(session_id);
    }
}

std::pair<Coordination::OpNum, Coordination::XID> NuKeeperTCPHandler::receiveRequest()
{
    int32_t length;
    Coordination::read(length, *in);
    int32_t xid;
    Coordination::read(xid, *in);

    Coordination::OpNum opnum;
    Coordination::read(opnum, *in);

    Coordination::ZooKeeperRequestPtr request = Coordination::ZooKeeperRequestFactory::instance().get(opnum);
    request->xid = xid;
    request->readImpl(*in);

    if (!nu_keeper_storage_dispatcher->putRequest(request, session_id))
        throw Exception(ErrorCodes::TIMEOUT_EXCEEDED, "Session {} already disconnected", session_id);
    return std::make_pair(opnum, xid);
}

}

#endif
