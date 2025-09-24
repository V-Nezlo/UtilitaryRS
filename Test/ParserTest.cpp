
#include "RsHandler.hpp"
#include "RsTypes.hpp"
#include <Crc8.hpp>
#include <cstdint>
#include <iostream>

#define GEN_BUF

// NOLINTBEGIN
void printBuffer(const uint8_t *buffer, size_t aSize)
{
	for (int i = 0; i < aSize; ++i) { std::cout << std::hex << static_cast<unsigned>(buffer[i]) << " "; }
}

bool createAndParseAckMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::AckMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Ack;
	message.payload.code = 0;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Ack message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Ack parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseCommandMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::ComMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0xAB;
	message.messageType = RS::MessageType::Command;
	message.payload.command = 0x06;
	message.payload.value = 0x07;
	message.payload.reserved = 0x08;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Command message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Command parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseProbeMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::ProbeMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Probe;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Probe message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Probe parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseRebootMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::RebootMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Reboot;
	message.payload.magic = 0xAABBCCDD;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Reboot message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Reboot parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseDeviceInfoRequestMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::DeviceInfoReqMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::DeviceInfoReq;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "DeviceInfoReq message parsed" << std::endl;
		return true;
	} else {
		std::cout << "DeviceInfoReq parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseBlobRequestMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::BlobReqMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::BlobRequest;
	message.payload.request = 0x02;
	message.payload.answerDataSize = 0x4;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Request message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Request parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseFileWriteRequestMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::FileWriteRequestMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::FileWriteRequest;
	message.payload.fileNumber = 1;
	message.payload.fileSize = 0xFFFF;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "FileWriteReq message parsed" << std::endl;
		return true;
	} else {
		std::cout << "FileWriteReq parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseFileWriteFinalizeMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::FileWriteFinalizeMessage message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::FileWriteFinalize;
	message.payload.fileNum = 1;
	message.payload.chunksNumber = 20;
	message.payload.crc = 0xAABBCCDD;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "FileWriteFinalize message parsed" << std::endl;
		return true;
	} else {
		std::cout << "FileWriteFinalize parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseBlobAnswerMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::BlobAnwMessage message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	struct Data {
		uint8_t data1{0x8};
		uint8_t data2{0x9};
		uint8_t data3{0xA};
		uint8_t data4{0xB};
	} payload;

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::BlobAnswer;

	message.payload.request = 0x07;
	message.payload.dataSize = sizeof(Data);

	memcpy(preBuffer, &message, sizeof(message));
	memcpy(&preBuffer[sizeof(message)], &payload, sizeof(Data));

	size_t length = parser.create(buffer, preBuffer, sizeof(message) + sizeof(payload));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "Answer message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Answer parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseWriteChunkMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::FileWriteChunkMessage message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	uint8_t chunk[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::FileWriteChunk;

	message.payload.fileNum = 1;
	message.payload.chunkSize = sizeof(chunk);

	memcpy(preBuffer, &message, sizeof(message));
	memcpy(&preBuffer[sizeof(message)], &chunk, sizeof(chunk));

	size_t length = parser.create(buffer, preBuffer, sizeof(message) + sizeof(chunk));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "WriteChunk message parsed" << std::endl;
		return true;
	} else {
		std::cout << "WriteChunk parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseDeviceInfoAnwMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::DeviceInfoAnwMessage message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::DeviceInfoAnw;
	message.number = 10;

	message.payload.version.hwRevision = 2;
	message.payload.version.swMajor = 1;
	message.payload.version.swMinor = 5;
	message.payload.version.reserved = 0;
	message.payload.version.swRevision = 0x80;
	message.payload.version.hash = 0xAABBCCDD;

	const char *name = "RRRRR";
	message.payload.nameLen = strlen(name);

	memcpy(preBuffer, &message, sizeof(message));
	memcpy(&preBuffer[sizeof(message)], name, message.payload.nameLen);

	size_t length = parser.create(buffer, preBuffer, sizeof(message) + message.payload.nameLen);
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "DeviceInfoAnswer message parsed" << std::endl;
		return true;
	} else {
		std::cout << "DeviceInfoAnswer parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseHealthReqMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::HealthReqMessage message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::HealthReq;
	message.number = 10;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "HealthReq message parsed" << std::endl;
		return true;
	} else {
		std::cout << "HealthReq parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseHealthAnwMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::HealthAnwMessage message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::HealthAnw;
	message.number = 10;

	message.payload.health = RS::Health::Healhy;
	message.payload.flags = 7;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.isReady()) {
		std::cout << "HealthAnw message parsed" << std::endl;
		return true;
	} else {
		std::cout << "HealthAnw parsing failed" << std::endl;
		return false;
	}
}

int main()
{
	bool success = true;

	success &= createAndParseAckMessage();
	success &= createAndParseCommandMessage();
	success &= createAndParseProbeMessage();
	success &= createAndParseRebootMessage();
	success &= createAndParseDeviceInfoRequestMessage();
	success &= createAndParseBlobRequestMessage();
	success &= createAndParseFileWriteRequestMessage();
	success &= createAndParseFileWriteFinalizeMessage();
	success &= createAndParseBlobAnswerMessage();
	success &= createAndParseWriteChunkMessage();
	success &= createAndParseDeviceInfoAnwMessage();
	success &= createAndParseHealthReqMessage();
	success &= createAndParseHealthAnwMessage();

	return !success;
}

// NOLINTEND
