#pragma once

#include "stdafx.h"

#define DEBUG_LEVEL 1

class Logger
{
private:
	Logger(void);

public:
	static void Error(const TCHAR* format, ...);
	static void ErrorWin32(DWORD dwError, const TCHAR* format, ...);

	static void Debug1(const TCHAR* format, ...);
#if DEBUG_LEVEL >= 2
	static void Debug2(const TCHAR* format, ...);
#else
	static void Debug2(const TCHAR* format, ...) {}
#endif
};
