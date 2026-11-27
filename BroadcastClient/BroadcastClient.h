#pragma once

#include "ClientSession.h"
#include "ConnectionId.h"
#include "IOCPClient.h"
#include "Packet.h"

#include <condition_variable> // for std::condition_variable
#include <cstdint>            // for std::uint32_t
#include <mutex>              // for std::mutex
#include <string_view>        // for std::string_view

class BroadcastClient
{
public:
    BroadcastClient() = default;

    BroadcastClient(const BroadcastClient&) = delete;
    BroadcastClient& operator=(const BroadcastClient&) = delete;

public:
    int RunClient();

private:
    void OnConnected(IOCPNetwork::ClientSession& session);
    void OnDisconnected(IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info);
    void OnConnectFailed(IOCPNetwork::DisconnectInfo info);

    void OnBroadcastPacket(IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet);

    [[nodiscard]] bool SendBroadcast(std::string_view name, std::string_view message, std::uint32_t sequence);

private:
    std::mutex m_stateMutex;
    std::condition_variable m_stateChanged;

    IOCPNetwork::ConnectionId m_connectionId{};

    bool m_connected = false;
    bool m_ended = false;

    IOCPNetwork::IOCPClient m_client;
};

