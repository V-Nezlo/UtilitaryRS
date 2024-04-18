#if not defined MOCKSERIAL_HPP and defined RSLIB_TESTING
#define MOCKSERIAL_HPP

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <string.h>
#include <vector>

class MockSerial {
public:
	MockSerial()
	{

	}

	size_t bytesAvaillable()
	{
		return 0;
	}

	size_t read(uint8_t *aBuffer, size_t aLength)
	{
		return 0;
	}

	size_t write(const uint8_t *aData, size_t aLength)
	{
		std::cout << "Writing to serial :";

		for (size_t i = 0; i < aLength; ++i) {
			vector.push_back(aData[i]);
			std::cout << std::hex << static_cast<unsigned>(aData[i]) << ' ';
		}

		std::cout << std::endl;
		return aLength;
	}

	uint8_t *data()
	{
		return vector.data();
	}

	size_t size()
	{
		return vector.size();
	}

private:
	std::vector<uint8_t> vector;
};

#endif // MOCKSERIAL_HPP
