#if not defined MOCKTIME_HPP and defined RSLIB_TESTING
#define MOCKTIME_HPP

#include <stdint.h>

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
