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

std::string retToString(Result aResult)
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
