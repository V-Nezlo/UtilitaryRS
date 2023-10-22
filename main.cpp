#include <iostream>
#include <RsParser.hpp>
#include <RsHandler.hpp>
#include "Mocks/MockSerial.hpp"
#include "Crc8.hpp"

int main(int, char**)
{

//	MockSerial serial;
//	RS::RsHandler<MockSerial, Crc8, 100, 1> handler(serial);

//	while(true) {
//		uint8_t buffer[64];

//		for (auto bytesRead = serial.read(buffer, sizeof(buffer)); bytesRead > 0; ) {
//			handler.update(buffer, bytesRead);
//			bytesRead = serial.read(buffer, sizeof(buffer));
//		}
//	}
}
