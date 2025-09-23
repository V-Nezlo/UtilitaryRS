
#include "Mocks/MockSerial.hpp"
#include "RsHandler.hpp"
#include "RsTypes.hpp"
#include <Crc8.hpp>
#include <iostream>
#include <cstdint>
#include <assert.h>

// NOLINTBEGIN
template<class Interface, typename Crc, size_t ParserSize>
class MasterHandler : public RS::RsHandler<Interface, Crc, ParserSize> {
	using BaseType = RS::RsHandler<Interface, Crc, ParserSize>;
public:
	MasterHandler(const char *aName, RS::DeviceVersion &aVersion, uint8_t aNodeUID, Interface &aInterface):
		BaseType{aName, aVersion, aNodeUID, aInterface}
	{

	}

	void handleAck(uint8_t aTranceiverUID, uint8_t aNumber, RS::Result aReturnCode) override
	{
		if (aTranceiverUID == 0x01 && aReturnCode == 0) {
			context.ackReceived = true;
		}
	}

	// RsHandler interface
	RS::Result processBlobRequest(uint8_t aTransmitUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{
		return RS::Ok;
	}

	RS::Result handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{
		if (aCommand == 0x06 && aArgument == 0x07) {
			context.commandReceived = true;
			return RS::Ok;
		} else {
			return RS::InvalidArg;
		}
	}

	RS::Result handleFileWriteRequest(uint8_t aTranceiverUID, uint8_t aFile, uint32_t aFileSize) override
	{
		return RS::Ok;
	}

	RS::Result handleWriteChunk(uint8_t aTransmitUID, uint8_t aFileNum, const void *aChunkData, size_t aChunkLen) override
	{
		return RS::Ok;
	}

	RS::Result handleWriteChunkFinalize(uint8_t aTransmitUID, uint8_t aFileNum, uint16_t aChunkCount, uint64_t aFileCRC) override
	{
		return RS::Ok;
	}

	void handleDeviceInfoAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, RS::DeviceVersion aVersion, const void *aName, size_t nameLen) override
	{

	}

	RS::Result handleReboot(uint64_t aMagic) override
	{
		const uint64_t magicReboot = 0x11223344;

		// Обманка, пока не знаю что сделать
		if (aMagic == 0xAABBCCDD) {
			return RS::Wait;
		}

		return RS::InvalidArg;
	}

	RS::Result handleBlobAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, uint8_t aRequest, const uint8_t *aData, uint8_t aLength) override
	{
		return RS::Ok;
	}

	auto getContext() const
	{
		return context;
	}

private:
	struct {
		bool ackReceived = false;
		bool commandReceived = false;
		bool probeReceived = false;
		bool rebootReceived = false;
		bool deviceInfoReqReceived = false;
		bool writeBlobReceived = false;
		bool fileWriteReqReceived = false;
		bool fileWriteFinalizeReceived = false;
		bool blobAnwerReceived = false;
		bool writeChunkReceived = false;
		bool deviceInfoAnswerReceived = false;
	} context;
};

int main()
{
	bool commandAckReceived = false;
	bool probeAckReceived = false;
	bool rebootAckReceived = false;

//	Ack message parsed
	const uint8_t ackMessage[] = {0x52, 0xff, 0x01, 0x01, 0x00, 0x00, 0xed};

//	Command message parsed
	const uint8_t commandMessage[] = {0x52, 0xff, 0xab, 0xa, 0x1, 0x6, 0x7, 0x8, 0xed};

//	Probe message parsed
	const uint8_t probeMessage[] = {0x52, 0xff, 0x1, 0x0, 0x7, 0x8, 0xea};

//	Reboot message parsed
	const uint8_t rebootMsg[] = {0x52, 0xff, 0x1, 0x7, 0x8, 0xdd, 0xcc, 0xbb, 0xaa, 0x0, 0x0, 0x0, 0x0, 0x9b};

//	DeviceInfoReq message parsed
	const uint8_t deviceInfoReq[] = {0x52, 0xff, 0x1, 0x2, 0x7, 0x8, 0xa5};

//	Request message parsed
	const uint8_t blobReq[] = {0x52, 0xff, 0x1, 0x9, 0x2, 0x2, 0x4, 0xb7};

//	FileWriteReq message parsed
	const uint8_t fileWriteReq[] = {0x52, 0xff, 0x1, 0x4, 0x0, 0x1, 0xff, 0xff, 0x0, 0x0, 0xe9};

//	FileWriteFinalize message parsed
	const uint8_t fileWriteFinal[] = {0x52, 0xff, 0x1, 0x6, 0x9, 0x1, 0x14, 0x0, 0xdd, 0xcc, 0xbb, 0xaa, 0x0, 0x0, 0x0, 0x0, 0x18};

//	Answer message parsed
	const uint8_t blobAnswer[] = {0x52, 0xff, 0x1, 0xa, 0xbb, 0x7, 0x0, 0x4, 0x8, 0x9, 0xa, 0xb, 0xb3};

//	WriteChunk message parsed
	const uint8_t writeChunk[] = {0x52, 0xff, 0x1, 0x5, 0x7f, 0x1, 0x6, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x4c};

//	DeviceInfoAnswer message parsed
	const uint8_t deviceInfoAnswer[] = {0x52, 0xff, 0x1, 0x3, 0xa, 0x0, 0x2, 0x1, 0x5, 0x80, 0x0, 0x0, 0x0, 0xdd, 0xcc,
		0xbb, 0xaa, 0x0, 0x0, 0x0, 0x0, 0x5, 0x52, 0x52, 0x52, 0x52, 0x52, 0x4c};

	RS::DeviceVersion version;
	version.hwRevision = 2;
	version.swMajor = 1;
	version.swMinor = 5;
	version.reserved = 0;
	version.swRevision = 0x80;
	version.hash = 0xAABBCCDD;

	MockSerial serial;
	MasterHandler<MockSerial, Crc8, 100> handler("TestHandler", version, 0xFF, serial);

	//ACK
	// Проверяем факт парсинга
	handler.update(ackMessage, sizeof(ackMessage));

	// COMMAND
	// Проверяем факт парсинга и корректность ответа (ACK::OK)
	handler.update(commandMessage, sizeof(commandMessage));
	const uint8_t expectedCmdAck[] = {0x52, 0xab, 0xff, 0x1, 0x0, 0x0, 0x3};
	assert(serial.size() == sizeof(expectedCmdAck));
	if (!memcmp(serial.data(), expectedCmdAck, sizeof(expectedCmdAck))) {
		commandAckReceived = true;
	}
	serial.clear();

	// PROBE
	// Проверяем корректность ответа (ACK::OK)
	handler.update(probeMessage, sizeof(probeMessage));
	const uint8_t expectedProbeAck[] = {0x52, 0x01, 0xff, 0x1, 0x0, 0x0, 0x8D};
	if (!memcmp(serial.data(), expectedProbeAck, sizeof(expectedProbeAck))) {
		probeAckReceived = true;
	}
	serial.clear();

	// REBOOT
	handler.update(rebootMsg, sizeof(rebootMsg));
	const uint8_t expectedRebootAck[] = {0x52, 0x1, 0xff, 0x1, 0x0, 0x2, 0x31};

	//	uint8_t ackBuffer[] = {0x52, 0xff, 0x01, 0x03, 0x06, 0x00, 0x08};
	//	handler.update(ackBuffer, sizeof(ackBuffer));

	//	uint8_t answerBuffer[] = {0x52, 0xff, 0x01, 0x02, 0x06, 0x07, 0x10, 0x04, 0x08, 0x09, 0x0a, 0x0b, 0xa7};
	//	handler.update(answerBuffer, sizeof(answerBuffer));

	//	uint8_t requestBuffer[] = {0x52, 0xff, 0x01, 0x01, 0x06, 0x02, 0x04, 0x35};
	//	handler.update(requestBuffer, sizeof(requestBuffer));

	//	uint8_t probeBuffer[] = {0x52, 0xff, 0x01, 0x04, 0x06, 0x00, 0x72};
	//	handler.update(probeBuffer, sizeof(probeBuffer));

	//	// Просто вызовы функций для покрытия, пока без тестов
	//	handler.sendCommand(1, 1, 1);
	//	handler.sendRequest(1, 1, 1);
	//	uint8_t answer = 64;
	//	handler.sendAnswer(1, 1, 1, &answer, 1);

	//	bool allMessagesHandled = (isAckHandled && isAnswerHandled && isRequestHandled && isCmdHandled) ? true : false;

	//	return isError || !allMessagesHandled;
}
// NOLINTEND
