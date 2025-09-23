
#include <thread>
#if not defined MOCKTIME_HPP
#define MOCKTIME_HPP
#include <chrono>
#include <stdint.h>

class MockTime {
public:
	static std::chrono::milliseconds milliseconds()
	{
		using clk = std::chrono::steady_clock;
		static const auto t0 = clk::now();
		auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0);
		return delta;
	}

	static std::chrono::seconds seconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(milliseconds());
	}

	static void delay(std::chrono::milliseconds aMillis)
	{
		std::this_thread::sleep_for(aMillis);
	}
};

#endif // MOCKTIME_HPP
