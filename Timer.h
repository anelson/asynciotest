#pragma once

class Timer
{
private:
	Timer(const LARGE_INTEGER& value);

public:
	Timer();
	Timer(const Timer& src);
	~Timer(void);

	static Timer Now() {
		LARGE_INTEGER li = {0};
		::QueryPerformanceCounter(&li);

		return Timer(li);
	}

	static Timer MinValue() {
		LARGE_INTEGER zero = {0};
		return Timer(zero);
	}

	// Computes the time between two timers
	Timer operator-(const Timer& rhs);

	// Computers the number of seconds represented by the timer value
	operator double();

	double Seconds();

private:
	LARGE_INTEGER m_value;
};
