#pragma once

#include "PacketTypes.h"

#include <cstddef>     // for std::size_t, std::byte, std::to_integer
#include <cstdint>     // for std::uint16_t, std::uint32_t
#include <cstring>     // for std::memcpy
#include <optional>    // for std::optional, std::nullopt
#include <span>        // for std::span
#include <string_view> // for std::string_view

namespace IOCPBroadcast
{
	inline constexpr IOCPNetwork::PacketId BroadcastPacketId = 1001;

	inline constexpr std::size_t MaxNameBytes = 32;
	inline constexpr std::size_t MaxMessageBytes = 1024;

	inline constexpr std::size_t BroadcastPrefixSize = 6;

	inline constexpr std::size_t MaxBroadcastPayloadSize =
		BroadcastPrefixSize + MaxNameBytes + MaxMessageBytes;

	// 메시지번호[4바이트] 이름길이[2바이트] 이름[N바이트] 메시지[나머지 바이트]

	// 문자열은 Packet 본문을 참조하며 메모리를 소유하지 않는다.
	struct BroadcastMessageView
	{
		std::uint32_t sequence = 0;

		std::string_view name;
		std::string_view message;
	};

	// 필드 검증 후 필요한 본문 바이트 수를 반환한다.
	[[nodiscard]] inline std::optional<std::size_t> GetBroadcastPayloadSize(const BroadcastMessageView& view) noexcept
	{
		if (view.sequence == 0)
		{
			return std::nullopt;
		}

		if (view.name.empty() || view.name.size() > MaxNameBytes)
		{
			return std::nullopt;
		}

		if (view.message.empty() || view.message.size() > MaxMessageBytes)
		{
			return std::nullopt;
		}

		return BroadcastPrefixSize + view.name.size() + view.message.size();
	}

	// 정확한 크기의 버퍼에 기록하며, 실패하면 버퍼를 보존한다.
	// 입력 문자열과 출력 버퍼는 겹치지 않아야 한다.
	[[nodiscard]] inline bool WriteBroadcastPayload(std::span<std::byte> destination, const BroadcastMessageView& view) noexcept
	{
		const auto payloadSize = GetBroadcastPayloadSize(view);

		if (!payloadSize || destination.size() != *payloadSize)
		{
			return false;
		}

		const auto sequence = view.sequence;
		const auto nameSize = static_cast<std::uint16_t>(view.name.size());

		destination[0] = static_cast<std::byte>(sequence & 0xFFu);
		destination[1] = static_cast<std::byte>((sequence >> 8) & 0xFFu);
		destination[2] = static_cast<std::byte>((sequence >> 16) & 0xFFu);
		destination[3] = static_cast<std::byte>((sequence >> 24) & 0xFFu);

		destination[4] = static_cast<std::byte>(nameSize & 0xFFu);
		destination[5] = static_cast<std::byte>((nameSize >> 8) & 0xFFu);

		std::memcpy(destination.data() + BroadcastPrefixSize, view.name.data(), view.name.size());
		std::memcpy(destination.data() + BroadcastPrefixSize + view.name.size(), view.message.data(), view.message.size());

		return true;
	}

	// 길이와 필드를 검증하며, 실패하면 result를 보존한다.
	// 성공한 뷰의 수명은 source에 종속되며 UTF-8 유효성은 검사하지 않는다.
	[[nodiscard]] inline bool ReadBroadcastPayload(std::span<const std::byte> source, BroadcastMessageView& result) noexcept
	{
		if (source.size() < BroadcastPrefixSize || source.size() > MaxBroadcastPayloadSize)
		{
			return false;
		}

		const std::uint32_t sequence =
			std::to_integer<std::uint32_t>(source[0]) |
			(std::to_integer<std::uint32_t>(source[1]) << 8) |
			(std::to_integer<std::uint32_t>(source[2]) << 16) |
			(std::to_integer<std::uint32_t>(source[3]) << 24);

		const std::size_t nameSize =
			std::to_integer<std::size_t>(source[4]) |
			(std::to_integer<std::size_t>(source[5]) << 8);

		if (sequence == 0 || nameSize == 0 || nameSize > MaxNameBytes)
		{
			return false;
		}

		const std::size_t remainingBytes = source.size() - BroadcastPrefixSize;

		if (nameSize >= remainingBytes)
		{
			return false;
		}

		const std::size_t messageSize = remainingBytes - nameSize;

		if (messageSize > MaxMessageBytes)
		{
			return false;
		}

		const auto* text = reinterpret_cast<const char*>(source.data() + BroadcastPrefixSize);

		BroadcastMessageView parsed;
		parsed.sequence = sequence;
		parsed.name = std::string_view{ text, nameSize };
		parsed.message = std::string_view{ text + nameSize, messageSize };

		result = parsed;

		return true;
	}
}