#pragma once

#include "ClientSession.h"
#include "ConnectionId.h"
#include "IOCPServer.h"
#include "Packet.h"

#include <mutex>  // for std::mutex
#include <vector> // for std::vector

class BroadcastServer
{
public:
	BroadcastServer() = default;

	BroadcastServer(const BroadcastServer&) = delete;
	BroadcastServer& operator=(const BroadcastServer&) = delete;

	int RunServer();

private:
	void OnConnected(IOCPNetwork::ClientSession& session);
	void OnDisconnected(IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info);

	void OnBroadcastPacket(IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet);

	[[nodiscard]] std::vector<IOCPNetwork::ConnectionId> GetConnectionSnapshot();

private:
	std::mutex m_connectionsMutex;

	std::vector<IOCPNetwork::ConnectionId> m_connections;

	IOCPNetwork::IOCPServer m_server;
};

