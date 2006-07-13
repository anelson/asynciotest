#include "StdAfx.h"
#include ".\timer.h"

Timer::Timer()
{
	LARGE_INTEGER zero = {0};
	m_value = zero;
}

Timer::Timer(const LARGE_INTEGER& value)
{
	m_value = value;
}

Timer::Timer(const Timer& src)
{
	m_value = src.m_value;
}

Timer::~Timer(void)
{
}

	// Computes the time between two timers
Timer Timer::operator-(const Timer& rhs) {
	LARGE_INTEGER diff;
	diff.QuadPart = m_value.QuadPart - rhs.m_value.QuadPart;
	return Timer(diff);
}

	// Computers the number of seconds represented by the timer value
Timer::operator double() {
	return Seconds();
}

double Timer::Seconds() {
	LARGE_INTEGER freq = {0};
	::QueryPerformanceFrequency(&freq);

	return static_cast<double>(m_value.QuadPart) / static_cast<double>(freq.QuadPart);
}