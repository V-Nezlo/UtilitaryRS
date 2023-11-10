#include "Mocks/MockSerial.hpp"
#include "RsHandler.hpp"
#include <Crc8.hpp>
#include <iostream>

bool isError{false};

bool isCmdHandled{false};
bool isAckHandled{false};
bool isAnswerHandled{false};
bool isRequestHandled{false};

template<class Interface, typename Crc, size_t ParserSize>
class TestHandler : public RS::RsHandler<Interface, Crc, ParserSize> {
	using BaseType = RS::RsHandler<Interface, Crc, ParserSize>;
public:
	TestHandler(Interface &aInterface, uint8_t aNodeUID):
		BaseType{aInterface, aNodeUID}
	{

	}

	uint8_t handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{
		std::cout << "COMMAND HANDLED" << std::endl;
		isCmdHandled = true;
		return 1;
	}

	void handleAck(uint8_t aReturnCode) override
	{
		std::cout << "ACKNOWLEDGE RECEIVED" << std::endl;
		isAckHandled = true;
	}

	uint8_t handleAnswer(uint8_t aRequest, const uint8_t *aData, uint8_t aLength) override
	{
		uint8_t buffer[4] __attribute__((aligned(8)));
		isAnswerHandled = true;

		memcpy(buffer, aData, aLength);

		if (aRequest == 7) {
			if (buffer[0] == 0x08 && buffer[1] == 0x09 && buffer[2] == 0x0a && buffer[3] == 0x0b) {
				std::cout << "ANSWER HANDLE SUCCESS" << std::endl;
				return 1;
			} else {
				isError = true;
				std::cout << "ANSWER HANDLE FAILED" << std::endl;
				return 0;
			}
		} else if (aRequest == 2 && aLength == 4) {
			const uint32_t *ptr = reinterpret_cast<const uint32_t *>(aData);
			if (*ptr == 0xAABBCCDD) {
				std::cout << "ANSWER AFTER REQUEST HANDLE SUCCESS" << std::endl;
			} else {
				isError = true;
				std::cout << "ANSWER AFTER REQUEST HANDLE FAILED" << std::endl;
			}
		}
		return 0;
	}

	bool processRequest(uint8_t aTransmitUID, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{
		std::cout << "REQUEST HANDLE RECEIVED, FORMING ANSWER" << std::endl;
		isRequestHandled = true;

		if (aRequest == 2) {
			uint32_t data = 0xAABBCCDD;
			return BaseType::sendAnswer(aTransmitUID, aRequest, aRequestedDataSize, &data, sizeof(data));
		}
		isError = true;
		return false;
	}
};

int main()
{
	MockSerial serial1;
	TestHandler<MockSerial, Crc8, 100> handler(serial1, 0xFF);

	uint8_t commandBuffer[] = {0x52, 0xff, 0xab, 0x00, 0x05, 0x06, 0x07, 0x08, 0x05e};
	handler.update(commandBuffer, sizeof(commandBuffer));

	uint8_t ackBuffer[] = {0x52, 0xff, 0x01, 0x03, 0x06, 0x00, 0x08};
	handler.update(ackBuffer, sizeof(ackBuffer));

	uint8_t answerBuffer[] = {0x52, 0xff, 0x01, 0x02, 0x06, 0x07, 0x10, 0x04, 0x08, 0x09, 0x0a, 0x0b, 0xa7};
	handler.update(answerBuffer, sizeof(answerBuffer));

	uint8_t requestBuffer[] = {0x52, 0xff, 0x01, 0x01, 0x06, 0x02, 0x04, 0x35};
	handler.update(requestBuffer, sizeof(requestBuffer));

	uint8_t probeBuffer[] = {0x52, 0xff, 0x01, 0x04, 0x06, 0x00, 0x72};
	handler.update(probeBuffer, sizeof(probeBuffer));

	bool allMessagesHandled = (isAckHandled && isAnswerHandled && isRequestHandled && isCmdHandled) ? true : false;

	return isError || !allMessagesHandled;
}
