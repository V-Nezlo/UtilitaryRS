
#include "Mocks/MockSerial.hpp"
#include <UtilitaryRS/RsHandler.hpp>
#include <UtilitaryRS/RsTypes.hpp>
#include <UtilitaryRS/Crc8.hpp>
#include <UtilitaryRS/MultiNode.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>

// NOLINTBEGIN
template<class Interface, typename Crc, size_t ParserSize>
class Device1 : public RS::RsHandler<Interface, Crc, ParserSize> {
	using BaseType = RS::RsHandler<Interface, Crc, ParserSize>;
public:
	Device1(const char *aName, RS::DeviceVersion &aVersion, uint8_t aNodeUID, Interface &aInterface):
		BaseType{aName, aVersion, aNodeUID, aInterface}
	{

	}

	void handleAck(uint8_t aTranceiverUID, uint8_t aNumber, RS::Result aReturnCode) override
	{

	}

	// RsHandler interface
	RS::Result processBlobRequest(uint8_t aTransmitUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{
		return RS::Ok;
	}

	RS::Result handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{
		if (aCommand == 0xAA && aArgument == 0x07) {
			context.cmdReceived = true;
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
		return;
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
		bool cmdReceived = false;
	} context;
};

template<class Interface, typename Crc, size_t ParserSize>
class Device2 : public RS::RsHandler<Interface, Crc, ParserSize> {
	using BaseType = RS::RsHandler<Interface, Crc, ParserSize>;
public:
	Device2(const char *aName, RS::DeviceVersion &aVersion, uint8_t aNodeUID, Interface &aInterface):
		BaseType{aName, aVersion, aNodeUID, aInterface}
	{

	}

	void handleAck(uint8_t aTranceiverUID, uint8_t aNumber, RS::Result aReturnCode) override
	{

	}

	// RsHandler interface
	RS::Result processBlobRequest(uint8_t aTransmitUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{
		return RS::Ok;
	}

	RS::Result handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{
		if (aCommand == 0xBB && aArgument == 0x07) {
			context.cmdReceived = true;
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
		return;
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
		bool cmdReceived = false;
	} context;
};

size_t createCommandMessage(uint8_t *aBuffer, uint8_t aReceiverUID, uint8_t aCmd)
{
	RS::RsParser<100, Crc8> parser;
	RS::ComMessage message;
	uint8_t buffer[100];

	message.receiverUID = aReceiverUID;
	message.transmitUID = 0xAB;
	message.messageType = RS::MessageType::Command;
	message.payload.command = aCmd;
	message.payload.value = 0x07;
	message.payload.reserved = 0x08;

	size_t len = parser.create(aBuffer, &message, sizeof(message));
	return len;
}

int main()
{
	bool commandAckReceived = false;
	bool probeAckReceived = false;
	bool rebootAckReceived = false;

	RS::DeviceVersion version;
	version.hwRevision = 2;
	version.swMajor = 1;
	version.swMinor = 5;
	version.reserved = 0;
	version.swRevision = 0x80;
	version.hash = 0xAABBCCDD;

	MockSerial serial;

	Device1<MockSerial, Crc8, 100> device1("Device1", version, 1, serial);
	Device2<MockSerial, Crc8, 100> device2("Device2", version, 2, serial);

	RS::MultiNode<256, Crc8, decltype(device1), decltype(device2)> composite(std::tie(device1, device2));

	uint8_t message1[64];
	uint8_t message2[64];

	size_t len1 = createCommandMessage(message1, 1, 0xAA);
	size_t len2 = createCommandMessage(message2, 2, 0xBB);

	composite.update(message1, len1);
	composite.update(message2, len2);

	const bool device1Ready = device1.getContext().cmdReceived;
	const bool device2Ready = device2.getContext().cmdReceived;

	return !(device1Ready || device2Ready);
}
// NOLINTEND
