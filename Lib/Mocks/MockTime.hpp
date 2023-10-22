#ifndef MOCKTIME_HPP
#define MOCKTIME_HPP

#include <cstdint>

class MockTime {
public:
	uint32_t milliseconds()
	{
		return 0;
	}

	uint32_t seconds()
	{
		return 0;
	}
};

#endif // MOCKTIME_HPP
