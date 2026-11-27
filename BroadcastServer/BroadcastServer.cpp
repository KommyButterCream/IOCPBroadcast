#include "BroadcastServer.h"

#include "../Common/BroadcastProtocol.h"

#include <algorithm>  // for std::find
#include <cstddef>    // for std::size_t
#include <exception>  // for std::exception
#include <iostream>   // for std::cin, std::cout, std::cerr
#include <mutex>      // for std::lock_guard
#include <string>     // for std::string, std::getline
#include <syncstream> // for std::osyncstream
#include <utility>    // for std::move
#include <vector>     // for std::erase
#include <chrono>     // for std::chrono::hours

namespace
{
	constexpr bool EnableDebugTimeouts = false;
	constexpr std::chrono::hours DebugTimeout{ 1 };
}

int BroadcastServer::RunServer()
{
	IOCPNetwork::ConnectionEvents events;

	events.connected = [this](IOCPNetwork::ClientSession& session)
		{
			OnConnected(session);
		};

	events.disconnected = [this](IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info)
		{
			OnDisconnected(session, info);
		};

	if (!m_server.SetConnectionEvents(std::move(events)))
	{
		std::osyncstream{ std::cerr }
		<< "Failed to register connection events.\n";

		return 1;
	}

	const auto registration = m_server.RegisterPacketHandler(
		IOCPBroadcast::BroadcastPacketId,
		[this](IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet)
		{
			OnBroadcastPacket(session, std::move(packet));
		});
	
	if (registration != IOCPNetwork::HandlerRegistrationResult::Registered)
	{
		std::osyncstream{ std::cerr }
			<< "Failed to register broadcast handler. result="
			<< static_cast<int>(registration)
			<< '\n';

		return 1;
	}

	IOCPNetwork::ServerOptions options;
	options.bindAddress = "127.0.0.1";
	options.port = 9000;
	options.maxConnections = 16;
	options.network.ioWorkerCount = 2;
	options.network.jobWorkerCount = 2;
	options.network.maxPacketPoolBytes = 16 * 1024 * 1024;

	if (EnableDebugTimeouts)
	{
		options.network.handshakeTimeout = DebugTimeout;
		options.network.heartbeatTimeout = DebugTimeout;
		options.network.receiveTimeout = DebugTimeout * 2;
		options.network.backpressureTimeout = DebugTimeout;
		options.network.shutdownTimeout = DebugTimeout;
	}

	{
		std::lock_guard<std::mutex> lock{ m_connectionsMutex };

		m_connections.reserve(options.maxConnections);
	}

	const auto startResult = m_server.Start(options);

	if (!startResult)
	{
		std::osyncstream{ std::cerr }
			<< "Failed to start server. error="
			<< static_cast<int>(startResult.error)
			<< ", nativeError=" << startResult.nativeError
			<< '\n';

		return 1;
	}

	std::osyncstream{ std::cout }
		<< "BroadcastServer listening on "
		<< options.bindAddress << ':' << options.port
		<< "\nPress Enter to stop.\n";

	std::string input;
	std::getline(std::cin, input);

	std::osyncstream{ std::cout }
	<< "Stopping BroadcastServer...\n";

	const auto stopResult = m_server.Stop();

	if (stopResult != IOCPNetwork::StopResult::Stopped &&
		stopResult != IOCPNetwork::StopResult::AlreadyStopped)
	{
		std::osyncstream{ std::cerr }
			<< "Failed to stop server. result="
			<< static_cast<int>(stopResult)
			<< '\n';

		return 1;
	}

	const auto remainingConnections = GetConnectionSnapshot();

	if (!remainingConnections.empty())
	{
		std::osyncstream{ std::cerr }
			<< "Connection list was not empty after Stop. count="
			<< remainingConnections.size()
			<< '\n';

		return 1;
	}

	std::osyncstream{ std::cout }
	<< "BroadcastServer stopped.\n";

	return 0;
}

void BroadcastServer::OnConnected(IOCPNetwork::ClientSession& session)
{
	const auto id = session.GetConnectionId();

	std::size_t connectionCount = 0;

	{
		std::lock_guard<std::mutex> lock{ m_connectionsMutex };

		const auto position = std::find(m_connections.begin(), m_connections.end(), id);

		if (position != m_connections.end())
		{
			return;
		}

		m_connections.push_back(id);

		connectionCount = m_connections.size();
	}

	std::osyncstream{ std::cout }
		<< "[Connected] index=" << id.index
		<< ", generation=" << id.generation
		<< ", connections=" << connectionCount
		<< '\n';
}

void BroadcastServer::OnDisconnected(IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info)
{
	const auto id = session.GetConnectionId();

	std::size_t connectionCount = 0;

	{
		std::lock_guard<std::mutex> lock{ m_connectionsMutex };

		std::erase(m_connections, id);

		connectionCount = m_connections.size();
	}

	std::osyncstream{ std::cout }
		<< "[Disconnected] index=" << id.index
		<< ", generation=" << id.generation
		<< ", connections=" << connectionCount
		<< ", reason=" << static_cast<int>(info.reason)
		<< ", nativeError=" << info.nativeError
		<< ", shutdownTimedOut=" << info.shutdownTimedOut
		<< '\n';
}

void BroadcastServer::OnBroadcastPacket(IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet)
{
	const auto senderId = session.GetConnectionId();

	IOCPBroadcast::BroadcastMessageView message;

	if (!IOCPBroadcast::ReadBroadcastPayload(packet.View().payload, message))
	{
		std::osyncstream{ std::cerr }
			<< "[Invalid broadcast] index=" << senderId.index
			<< ", generation=" << senderId.generation
			<< '\n';

		return;
	}

	const auto targets = GetConnectionSnapshot();

	std::size_t queuedCount = 0;

	for (const auto targetId : targets)
	{
		// 패킷을 복사하지 않고 동일한 패킷 저장소의 소유 참조를 추가
		IOCPNetwork::Packet outgoing = packet;

		const auto result = m_server.Send(targetId, std::move(outgoing));

		if (result == IOCPNetwork::SendResult::Queued)
		{
			++queuedCount;

			continue;
		}

		std::osyncstream{ std::cerr }
			<< "[Broadcast rejected] targetIndex=" << targetId.index
			<< ", targetGeneration=" << targetId.generation
			<< ", result=" << static_cast<int>(result)
			<< '\n';
	}

	std::osyncstream{ std::cout }
		<< "[Broadcast queued] senderIndex=" << senderId.index
		<< ", name=" << message.name
		<< ", sequence=" << message.sequence
		<< ", targets=" << targets.size()
		<< ", queued=" << queuedCount
		<< '\n';
}

std::vector<IOCPNetwork::ConnectionId> BroadcastServer::GetConnectionSnapshot()
{
	std::lock_guard<std::mutex> lock{ m_connectionsMutex };

	return m_connections;
}

int main()
{
	try
	{
		BroadcastServer server;

		return server.RunServer();
	}
	catch (const std::exception& exception)
	{
		std::osyncstream{ std::cerr }
			<< "BroadcastServer exception: "
			<< exception.what()
			<< '\n';

		return 1;
	}
}
