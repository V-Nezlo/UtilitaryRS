#ifndef LIB_RSHUB_HPP
#define LIB_RSHUB_HPP

#include <stddef.h>
#include "RsHandler.hpp"

namespace RS {

template<size_t NodeCount, class Interface, typename Crc, size_t ParserSize, typename ...Devices>
class RsHub : public RsHandler<Interface, Crc, ParserSize> {
	using BaseType = RS::RsHandler<Interface, Crc, ParserSize>;
public:
	RsHub(Interface &aInterface, uint8_t aNodeUID) : BaseType{aInterface, aNodeUID}
	{

	}

	// RsHandler interfaces
	bool processRequest(uint8_t aTransmitUID, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{

	}
	uint8_t handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{

	}
	void handleAck(uint8_t aTranceiverUID, uint8_t aReturnCode) override
	{

	}
	uint8_t handleAnswer(uint8_t aTranceiverUID, uint8_t aRequest, const uint8_t *aData, uint8_t aLength) override
	{

	}
private:
	std::tuple<Devices...> devices;
};

}



#endif // RSHUB_HPP
