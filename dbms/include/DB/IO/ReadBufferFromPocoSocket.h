#pragma once

#include <Poco/Net/Socket.h>
#include <Poco/Net/NetException.h>

#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>

#include <DB/IO/ReadBuffer.h>
#include <DB/IO/BufferWithOwnMemory.h>


namespace DB
{

/** Работает с готовым Poco::Net::Socket. Операции блокирующие.
  */
class ReadBufferFromPocoSocket : public BufferWithOwnMemory<ReadBuffer>
{
protected:
	Poco::Net::Socket & socket;

	/** Для сообщений об ошибках. Нужно получать этот адрес заранее, так как,
	  *  например, если соединение разорвано, то адрес уже будет получить нельзя
	  *  (getpeername вернёт ошибку).
	  */
	Poco::Net::SocketAddress peer_address;

	
	bool nextImpl()
	{
		ssize_t bytes_read = 0;
		
		/// Добавляем в эксепшены более подробную информацию.
		try
		{
			bytes_read = socket.impl()->receiveBytes(internal_buffer.begin(), internal_buffer.size());
		}
		catch (const Poco::Net::NetException & e)
		{
			throw Exception(e.displayText(), "while reading from socket (" + peer_address.toString() + ")", ErrorCodes::NETWORK_ERROR);
		}
		catch (const Poco::TimeoutException & e)
		{
			throw Exception("Timeout exceeded while reading from socket (" + peer_address.toString() + ")", ErrorCodes::SOCKET_TIMEOUT);
		}
		catch (const Poco::IOException & e)
		{
			throw Exception(e.displayText(), "while reading from socket (" + peer_address.toString() + ")", ErrorCodes::NETWORK_ERROR);
		}
		
		if (bytes_read < 0)
			throw Exception("Cannot read from socket (" + peer_address.toString() + ")", ErrorCodes::CANNOT_READ_FROM_SOCKET);

		if (bytes_read)
			working_buffer.resize(bytes_read);
		else
			return false;

		return true;
	}

public:
	ReadBufferFromPocoSocket(Poco::Net::Socket & socket_, size_t buf_size = DBMS_DEFAULT_BUFFER_SIZE)
		: BufferWithOwnMemory<ReadBuffer>(buf_size), socket(socket_), peer_address(socket.peerAddress())
	{
	}

	bool poll(size_t timeout_microseconds)
	{
		return offset() != buffer().size() || socket.poll(timeout_microseconds, Poco::Net::Socket::SELECT_READ | Poco::Net::Socket::SELECT_ERROR);
	}
};

}
