/*!
\file
\brief Полезные функции
\author V-Nezlo (vlladimirka@gmail.com)
\date 16.09.2025
\version 2.0

*/

#ifndef LIB_RSHELPERS_HPP
#define LIB_RSHELPERS_HPP

#include "RsTypes.hpp"
#include <string>
#include <string.h>

namespace RS::Helpers {

/// \brief Получить хеш-группу ноды для раунда обнаружения
/// FNV-1a по UID и байтам nonce от младшего к старшему, затем перемешивание всех битов
static constexpr uint8_t getAllocationBucket(const NodeUid &aUID, uint32_t aRoundNonce)
{
	uint32_t hash{2166136261u};
	for (uint8_t byte : aUID) {
		hash = (hash ^ byte) * 16777619u;
	}
	for (unsigned shift = 0; shift < 32; shift += 8) {
		hash = (hash ^ static_cast<uint8_t>(aRoundNonce >> shift)) * 16777619u;
	}
	hash ^= hash >> 16;
	hash *= 0x85EBCA6Bu;
	hash ^= hash >> 13;
	hash *= 0xC2B2AE35u;
	hash ^= hash >> 16;
	return static_cast<uint8_t>(hash % kAllocationBucketCount);
}

/// \brief Получить длину сообщения из типа
/// \param aType тип сообщения
/// \return длина сообщения или 0 если сообщение переменного размера
static constexpr size_t getMessageSizeByType(MessageType aType)
{
	switch(aType) {
		case MessageType::Ack:
			return sizeof(AckMessage);
		case MessageType::Command:
			return sizeof(ComMessage);
		case MessageType::Probe:
			return sizeof(ProbeMessage);
		case MessageType::Reboot:
			return sizeof(RebootMessage);
		case MessageType::DeviceInfoReq:
			return sizeof(DeviceInfoReqMessage);
		case MessageType::BlobRequest:
			return sizeof(BlobReqMessage);
		case MessageType::FileWriteFinalize:
			return sizeof(FileWriteFinalizeMessage);
		case MessageType::FileWriteRequest:
			return sizeof(FileWriteRequestMessage);
		case MessageType::HealthReq:
			return sizeof(HealthReqMessage);
		case MessageType::HealthAnw:
			return sizeof(HealthAnwMessage);
		case MessageType::DiscoverReq:
			return sizeof(DiscoverReqMessage);
		case MessageType::DiscoverAnw:
			return sizeof(DiscoverAnwMessage);
		case MessageType::AssignAddressReq:
			return sizeof(AssignAddressReqMessage);
		case MessageType::AssignAddressAnw:
			return sizeof(AssignAddressAnwMessage);

		default:
			return 0;
	}
}

static constexpr size_t getVolatileMessageBaseSize(MessageType aType)
{
	switch(aType) {
		case MessageType::BlobAnswer:
			return sizeof(BlobAnwMessage);
		case MessageType::FileWriteChunk:
			return sizeof(FileWriteChunkMessage);
		case MessageType::DeviceInfoAnw:
			return sizeof(DeviceInfoAnwMessage);
		default:
			return 0;
	}
}

static constexpr size_t getVolatileMessageMaxPayloadSize(MessageType aType)
{
	switch(aType) {
		case MessageType::BlobAnswer:
			return 0xFF;
		case MessageType::FileWriteChunk:
			return 0xFF;
		case MessageType::DeviceInfoAnw:
			return 0xFF;
		default:
			return 0;
	}
}

inline std::string retToString(Result aResult)
{
	switch (aResult) {
		case Result::Busy:
			return "Busy";
		case Result::ChecksumFailed:
			return "Checksum failed";
		case Result::Error:
			return "Error";
		case Result::InvalidArg:
			return "Invalid argument";
		case Result::Ok:
			return "Success";
		case Result::Wait:
			return "Wait";
		case Result::Timeout:
			return "Timeout";
		case Result::Unsupported:
			return "Unsupported";
		default:
			return "Custom return code: " + std::to_string(static_cast<unsigned>(aResult));
	}
}

} // namespace RS

#endif // LIB_RSHELPERS_HPP
