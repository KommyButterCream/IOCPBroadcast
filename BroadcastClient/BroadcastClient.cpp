#include "BroadcastClient.h"

#include "PacketResult.h"
#include "PacketWriter.h"

#include "../Common/BroadcastProtocol.h"

#include <chrono>     // for std::chrono::hours, std::chrono::milliseconds
#include <cstdint>    // for std::uint32_t
#include <exception>  // for std::exception
#include <iostream>   // for std::cin, std::cout, std::cerr
#include <limits>     // for std::numeric_limits
#include <mutex>      // for std::lock_guard, std::unique_lock
#include <string>     // for std::string, std::getline
#include <string_view> // for std::string_view
#include <syncstream> // for std::osyncstream
#include <utility>    // for std::move

namespace
{
    // 중단점에서 오래 대기할 때 서버 설정과 함께 활성화한다.
    constexpr bool EnableDebugTimeouts = false;
    constexpr std::chrono::hours DebugTimeout{ 1 };
}

int BroadcastClient::RunClient()
{
    IOCPNetwork::ConnectionEvents events;

    events.connected = [this](IOCPNetwork::ClientSession& session)
        {
            OnConnected(session);
        };

    events.disconnected =
        [this](
            IOCPNetwork::ClientSession& session,
            IOCPNetwork::DisconnectInfo info)
        {
            OnDisconnected(session, info);
        };

    events.connectFailed = [this](IOCPNetwork::DisconnectInfo info)
        {
            OnConnectFailed(info);
        };

    if (!m_client.SetConnectionEvents(std::move(events)))
    {
        std::osyncstream{ std::cerr }
        << "Failed to register connection events.\n";

        return 1;
    }

    const auto registration = m_client.RegisterPacketHandler(
        IOCPBroadcast::BroadcastPacketId,
        [this](
            IOCPNetwork::ClientSession& session,
            IOCPNetwork::Packet packet)
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

    std::string name;

    while (true)
    {
        std::osyncstream{ std::cout }
            << "Name (1-" << IOCPBroadcast::MaxNameBytes
            << " UTF-8 bytes): " << std::flush;

        if (!std::getline(std::cin, name))
        {
            return 0;
        }

        if (!name.empty() && name.size() <= IOCPBroadcast::MaxNameBytes)
        {
            break;
        }

        std::osyncstream{ std::cerr }
        << "Invalid name length.\n";
    }

    IOCPNetwork::ClientOptions options;
    options.serverAddress = "127.0.0.1";
    options.port = 9001;
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

    const auto startResult = m_client.Start(options);

    if (!startResult)
    {
        std::osyncstream{ std::cerr }
            << "Failed to start client. error="
            << static_cast<int>(startResult.error)
            << ", nativeError=" << startResult.nativeError
            << '\n';

        return 1;
    }

    const auto waitTimeout = EnableDebugTimeouts ?
        std::chrono::milliseconds{ DebugTimeout } : std::chrono::milliseconds{ 15'000 };

    bool ready = false;
    
    {
        std::unique_lock<std::mutex> lock{ m_stateMutex };

        const bool signaled = m_stateChanged.wait_for(
            lock,
            waitTimeout,
            [this]
            {
                return m_connected || m_ended;
            });

        ready = signaled && m_connected && !m_ended;
    }

    int exitCode = 0;

    if (!ready)
    {
        std::osyncstream{ std::cerr }
        << "Connection was not ready.\n";

        exitCode = 1;
    }
    else
    {
        std::osyncstream{ std::cout }
            << "Connected as " << name << ".\n"
            << "Enter a message, or /quit to stop.\n"
            << "If the server disconnects, press Enter to finish.\n";

        std::uint32_t sequence = 1;
        std::string message;

        while (true)
        {
            bool ended = false;

            {
                std::lock_guard<std::mutex> lock{ m_stateMutex };

                ended = m_ended;
            }

            if (ended)
            {
                break;
            }

            if (!std::getline(std::cin, message))
            {
                break;
            }

            {
                std::lock_guard<std::mutex> lock{ m_stateMutex };

                ended = m_ended;
            }

            if (ended || message == "/quit")
            {
                break;
            }

            if (message.empty())
            {
                continue;
            }

            if (message.size() > IOCPBroadcast::MaxMessageBytes)
            {
                std::osyncstream{ std::cerr }
                    << "Message is too long. maximum="
                    << IOCPBroadcast::MaxMessageBytes
                    << " bytes.\n";

                continue;
            }

            if (!SendBroadcast(name, message, sequence))
            {
                continue;
            }

            if (sequence == (std::numeric_limits<std::uint32_t>::max)())
            {
                std::osyncstream{ std::cout }
                << "Sequence limit reached.\n";

                break;
            }

            ++sequence;
        }
    }

    std::osyncstream{ std::cout }
    << "Stopping BroadcastClient...\n";

    const auto stopResult = m_client.Stop();

    if (stopResult != IOCPNetwork::StopResult::Stopped &&
        stopResult != IOCPNetwork::StopResult::AlreadyStopped)
    {
        std::osyncstream{ std::cerr }
            << "Failed to stop client. result="
            << static_cast<int>(stopResult)
            << '\n';

        return 1;
    }

    std::osyncstream{ std::cout }
    << "BroadcastClient stopped.\n";

    return exitCode;
}

void BroadcastClient::OnConnected(IOCPNetwork::ClientSession& session)
{
    const auto id = session.GetConnectionId();

    {
        std::lock_guard<std::mutex> lock{ m_stateMutex };

        m_connectionId = id;
        m_connected = true;
        m_ended = false;
    }

    m_stateChanged.notify_one();

    std::osyncstream{ std::cout }
        << "[Connected] index=" << id.index
        << ", generation=" << id.generation
        << '\n';
}

void BroadcastClient::OnDisconnected(IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info)
{
    const auto id = session.GetConnectionId();

    {
        std::lock_guard<std::mutex> lock{ m_stateMutex };

        m_connectionId = {};
        m_connected = false;
        m_ended = true;
    }

    m_stateChanged.notify_one();

    std::osyncstream{ std::cout }
        << "[Disconnected] index=" << id.index
        << ", generation=" << id.generation
        << ", reason=" << static_cast<int>(info.reason)
        << ", nativeError=" << info.nativeError
        << ", shutdownTimedOut=" << info.shutdownTimedOut
        << '\n';
}

void BroadcastClient::OnConnectFailed(IOCPNetwork::DisconnectInfo info)
{
    {
        std::lock_guard<std::mutex> lock{ m_stateMutex };


        m_connectionId = {};
        m_connected = false;
        m_ended = true;
    }

    m_stateChanged.notify_one();

    std::osyncstream{ std::cerr }
        << "[Connect failed] reason="
        << static_cast<int>(info.reason)
        << ", nativeError=" << info.nativeError
        << ", shutdownTimedOut=" << info.shutdownTimedOut
        << '\n';
}

void BroadcastClient::OnBroadcastPacket(IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet)
{
    IOCPBroadcast::BroadcastMessageView message;

    if (!IOCPBroadcast::ReadBroadcastPayload(packet.View().payload, message))
    {
        const auto id = session.GetConnectionId();

        std::osyncstream{ std::cerr }
            << "[Invalid broadcast] connectionIndex=" << id.index
            << ", connectionGeneration=" << id.generation
            << '\n';

        return;
    }

    std::osyncstream{ std::cout }
        << "[Broadcast] name=" << message.name
        << ", sequence=" << message.sequence
        << ", message=" << message.message
        << '\n';
}

bool BroadcastClient::SendBroadcast(
    std::string_view name,
    std::string_view message,
    std::uint32_t sequence)
{
    const IOCPBroadcast::BroadcastMessageView value{ sequence, name, message };

    const auto payloadSize = IOCPBroadcast::GetBroadcastPayloadSize(value);

    if (!payloadSize)
    {
        std::osyncstream{ std::cerr }
        << "[Send rejected] Invalid name, message, or sequence.\n";

        return false;
    }

    IOCPNetwork::ConnectionId connectionId{};

    {
        std::lock_guard<std::mutex> lock{ m_stateMutex };

        if (!m_connected || m_ended)
        {
            return false;
        }

        connectionId = m_connectionId;
    }

    auto writerResult = m_client.CreatePacketWriter(IOCPBroadcast::BroadcastPacketId, *payloadSize);

    if (!writerResult)
    {
        std::osyncstream{ std::cerr }
            << "Failed to create packet writer. error="
            << static_cast<int>(writerResult.error)
            << '\n';

        return false;
    }

    if (!IOCPBroadcast::WriteBroadcastPayload(writerResult.writer.GetWritablePayload(), value))
    {
        std::osyncstream{ std::cerr }
        << "Failed to write broadcast payload.\n";

        return false;
    }

    auto packetResult = writerResult.writer.Finish(*payloadSize);

    if (!packetResult)
    {
        std::osyncstream{ std::cerr }
            << "Failed to finish packet. error="
            << static_cast<int>(packetResult.error)
            << '\n';

        return false;
    }

    const auto result = m_client.Send(connectionId, std::move(packetResult.packet));

    if (result != IOCPNetwork::SendResult::Queued)
    {
        std::osyncstream{ std::cerr }
            << "[Send rejected] sequence=" << sequence
            << ", result=" << static_cast<int>(result)
            << '\n';

        return false;
    }

    std::osyncstream{ std::cout }
        << "[Send queued] sequence=" << sequence
        << ", payloadBytes=" << *payloadSize
        << '\n';

    return true;
}

int main()
{
    try
    {
        BroadcastClient client;

        return client.RunClient();
    }
    catch (const std::exception& exception)
    {
        std::osyncstream{ std::cerr }
            << "BroadcastClient exception: "
            << exception.what()
            << '\n';

        return 1;
    }
}
