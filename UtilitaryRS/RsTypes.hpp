/*!
\file
\brief Файл, описывающий сообщения внутри протокола UtilitaryRS
\author V-Nezlo (vlladimirka@gmail.com)
\date 16.09.2025
\version 2.0
*/

#ifndef LIB_RSTYPES_HPP_
#define LIB_RSTYPES_HPP_

#include <stdint.h>

namespace RS {

static constexpr uint8_t kReservedUID{0xFF};

/// \brief Энумератор типов сообщений - сообщения имеют следующие типы: Команда, Запрос данных, Ответ на запрос данных,
/// подверждение приема сообщения
// clang-format off
enum class MessageType : uint8_t {
	// UtilitaryRS, версия 1
	Probe,
	Ack,
	Command,
	BlobRequest,
	BlobAnswer,
	// UtilitaryRS, версия 2
	DeviceInfoReq,
	DeviceInfoAnw,

	FileWriteRequest,
	FileWriteChunk,
	FileWriteFinalize,

	HealthReq,
	HealthAnw,

	Reboot,

	TypeEnd
};
// clang-format on

enum Result : uint8_t {
	Ok,
	Error,
	Wait,
	Busy,
	InvalidArg,
	Timeout,
	Unsupported,
	ChecksumFailed
};

enum class Health : uint8_t {
	WarnUp,
	Healhy,
	Warning,
	Error,
	Critical
};

struct DeviceVersion {
	uint8_t reserved;
	uint8_t hwRevision;
	uint8_t swMajor;
	uint8_t swMinor;
	uint32_t swRevision;
	uint64_t hash;
};

/// \brief Заголовок сообщения, содержит UID отправителя, UID получателя и тип сообщения
struct Header {
	uint8_t receiverUID;
	uint8_t transmitUID;
	MessageType messageType;
	uint8_t number;
} __attribute__((packed));

template<class T>
struct Packet : public Header {
	T payload;
} __attribute__((packed));

/// \brief Полезная нагрузка сообщения Probe, не содержит ничего (пока)
struct ProbePayload {
	uint8_t reserved;
} __attribute__((packed));

struct DeviceInfoReqPayload {
	uint8_t reserved;
} __attribute__((packed));

struct DeviceInfoAnwPayload {
	__attribute__((packed, aligned(1))) DeviceVersion version;
	uint8_t nameLen;
	// name
} __attribute__((packed));

struct FileWriteRequestPayload {
	uint8_t fileNumber;
	uint32_t fileSize;
} __attribute__((packed));

struct FileWriteChunkPayload {
	uint8_t fileNum;
	// Последний байт обязательно длина payload
	uint8_t chunkSize;
	// chunk;
} __attribute__((packed));

struct FileWriteFinalizePayload {
	uint8_t fileNum;
	uint16_t chunksNumber;
	uint64_t crc;
} __attribute__((packed));

struct HealthReqPayload {
	uint8_t reserved;
} __attribute__((packed));

struct HealthAnwPayload {
	Health health;
	uint8_t reserved;
	uint16_t flags;
} __attribute__((packed));

struct RebootPayload {
	uint64_t magic;
} __attribute__((packed));

/// \brief Полезная нагрузка командного сообщения, содержит номер команды и аргумент
struct CommandPayload {
	uint8_t command;
	uint8_t value;
	uint8_t reserved;
} __attribute__((packed));

/// \brief Полезная нагрузка сообщения запроса данных, содержит номер запроса и количество запрашиваемых байт
struct BlobReqPayload {
	uint8_t request;
	uint8_t answerDataSize;
} __attribute__((packed));

/// \brief Полезная нагрузка сообщения ответа на запрос, содержит номер запроса, размер данных запроса
struct BlobAnwPayload {
	uint8_t request;
	uint8_t reserved;
	// Последний байт обязательно длина payload
	uint8_t dataSize;
	// chunk;
} __attribute__((packed));

/// \brief Полезная нагрузка Ack сообщения, содержит код возврата
struct AckPayload {
	uint8_t code;
} __attribute__((packed));

using ProbeMessage = Packet<ProbePayload>;
using AckMessage = Packet<AckPayload>;

using DeviceInfoReqMessage = Packet<DeviceInfoReqPayload>;
using DeviceInfoAnwMessage = Packet<DeviceInfoAnwPayload>;

using FileWriteRequestMessage = Packet<FileWriteRequestPayload>;
using FileWriteChunkMessage = Packet<FileWriteChunkPayload>;
using FileWriteFinalizeMessage = Packet<FileWriteFinalizePayload>;

using HealthReqMessage = Packet<HealthReqPayload>;
using HealthAnwMessage = Packet<HealthAnwPayload>;

using RebootMessage = Packet<RebootPayload>;

using ComMessage = Packet<CommandPayload>;
using BlobReqMessage = Packet<BlobReqPayload>;
using BlobAnwMessage = Packet<BlobAnwPayload>;

} // namespace RS

#endif // LIB_RSTYPES_HPP_
