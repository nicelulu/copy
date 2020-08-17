#pragma once
#include <Core/Types.h>
#include <Core/MySQL/MySQLReplication.h>
#include <IO/ReadBufferFromPocoSocket.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteBufferFromPocoSocket.h>
#include <IO/WriteHelpers.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/StreamSocket.h>
#include <Common/DNSResolver.h>
#include <Common/Exception.h>
#include <Common/NetException.h>
#include <Core/MySQL/IMySQLWritePacket.h>


namespace DB
{

using namespace MySQLProtocol;
using namespace MySQLReplication;

class MySQLClient
{
public:
    MySQLClient(const String & host_, UInt16 port_, const String & user_, const String & password_);
    MySQLClient(MySQLClient && other);

    void connect();
    void disconnect();
    void ping();

    /// Start replication stream by binlog+position.
    /// replicate_db: replication database schema, events from other databases will be ignored.
    /// binlog_file_name: replication from which binlog.
    /// binlog_pos: replication from which position of the binlog.
    void startBinlogDump(UInt32 slave_id, String replicate_db, String binlog_file_name, UInt64 binlog_pos);

    /// Start replication stream by GTID.
    /// replicate_db: replication database schema, events from other databases will be ignored.
    /// gtid: executed gtid sets format like 'hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh:x-y'.
    void startBinlogDumpGTID(UInt32 slave_id, String replicate_db, String gtid);

    BinlogEventPtr readOneBinlogEvent(UInt64 milliseconds = 0);
    Position getPosition() const { return replication.getPosition(); }

private:
    String host;
    UInt16 port;
    String user;
    String password;

    bool connected = false;
    UInt32 client_capability_flags = 0;

    uint8_t seq = 0;
    const UInt8 charset_utf8 = 33;
    const String mysql_native_password = "mysql_native_password";

    MySQLFlavor replication;
    std::shared_ptr<ReadBuffer> in;
    std::shared_ptr<WriteBuffer> out;
    std::unique_ptr<Poco::Net::StreamSocket> socket;
    std::optional<Poco::Net::SocketAddress> address;
    std::shared_ptr<PacketEndpoint> packet_endpoint;

    void handshake();
    void registerSlaveOnMaster(UInt32 slave_id);
    void writeCommand(char command, String query);
};

class WriteCommand : public IMySQLWritePacket
{
public:
    char command;
    String query;

    WriteCommand(char command_, String query_) : command(command_), query(query_) { }

    size_t getPayloadSize() const override { return 1 + query.size(); }

    void writePayloadImpl(WriteBuffer & buffer) const override
    {
        buffer.write(static_cast<char>(command));
        if (!query.empty())
        {
            buffer.write(query.data(), query.size());
        }
    }
};
}
