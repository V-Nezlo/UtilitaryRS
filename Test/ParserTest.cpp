#include "RsHandler.hpp"
#include "RsTypes.hpp"
#include <Crc8.hpp>
#include <iostream>

void printBuffer(const uint8_t *buffer, size_t aSize)
{
	for (int i = 0; i < aSize; ++i) {
		std::cout << std::hex << static_cast<unsigned>(buffer[i]) << " ";
	}
}

bool createAndParseCommandMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::Packet<RS::CommandPayload> message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0xAB;
	message.messageType = RS::MessageType::Command;
	message.reserved = 0x05;
	message.payload.command = 0x06;
	message.payload.value = 0x07;
	message.payload.reserved = 0x08;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	if (parser.state() == RS::RsParser<100, Crc8>::State::Done) {
		std::cout << "Command message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Command parsing failed" << std::endl;
		return false;
	}


}

bool createAndParseRequestMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::Packet<RS::RequestPayload> message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Request;
	message.reserved = 0x06;
	message.payload.request = 0x02;
	message.payload.answerDataSize = 0x4;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	if (parser.state() == RS::RsParser<100, Crc8>::State::Done) {
		std::cout << "Request message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Request parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseAnswerMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::Packet<RS::AnswerPayload> message;
	uint8_t preBuffer[100];
	uint8_t buffer[100];

	struct Data{
		uint8_t data1{0x8};
		uint8_t data2{0x9};
		uint8_t data3{0xA};
		uint8_t data4{0xB};
	} payload;

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Answer;
	message.reserved = 0x06;
	message.payload.request = 0x07;
	message.payload.dataSize = sizeof(Data);

	memcpy(preBuffer, &message, sizeof(message));
	memcpy(&preBuffer[sizeof(message)], &payload, sizeof(Data));

	size_t length = parser.create(buffer, preBuffer, sizeof(message) + sizeof(payload));
	parser.update(buffer, length);

	if (parser.state() == RS::RsParser<100, Crc8>::State::Done) {
		std::cout << "Answer message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Answer parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseAckMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::Packet<RS::AckPayload> message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Ack;
	message.payload.code = 0;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	if (parser.state() == RS::RsParser<100, Crc8>::State::Done) {
		std::cout << "Ack message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Ack parsing failed" << std::endl;
		return false;
	}
}

bool createAndParseProbeMessage()
{
	RS::RsParser<100, Crc8> parser;
	RS::Packet<RS::ProbePayload> message;
	uint8_t buffer[100];

	message.receiverUID = 0xFF;
	message.transmitUID = 0x01;
	message.messageType = RS::MessageType::Probe;

	size_t length = parser.create(buffer, &message, sizeof(message));
	parser.update(buffer, length);

	printBuffer(buffer, length);

	if (parser.state() == RS::RsParser<100, Crc8>::State::Done) {
		std::cout << "Probe message parsed" << std::endl;
		return true;
	} else {
		std::cout << "Probe parsing failed" << std::endl;
		return false;
	}
}

int main()
{
	bool success = true;

	success = success & createAndParseCommandMessage();
	success = success & createAndParseRequestMessage();
	success = success & createAndParseAnswerMessage();
	success = success & createAndParseAckMessage();
	success = success & createAndParseProbeMessage();

	return !success;
}
