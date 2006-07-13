#include "StdAfx.h"
#include "Logger.h"

#include <stdarg.h>

void Logger::Error(const TCHAR* format, ...)
{   
	va_list argptr;
	va_start(argptr, format);

	_tprintf(_T("ERROR: "));
	_vtprintf(format, argptr);
	_tprintf(_T("\n"));
}

void Logger::ErrorWin32(DWORD dwError, const TCHAR* format, ...)
{   
	LPTSTR win32Msg = NULL;

	DWORD result = ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		dwError,
		0,
		reinterpret_cast<LPTSTR>(&win32Msg),
		1,
		NULL);

	if (result == 0) {
		//Failed to get message.  Build a string with the error number.
		win32Msg = reinterpret_cast<LPTSTR>(::LocalAlloc(0, 255));
		_stprintf(win32Msg, _T("Unknown Win32 Error code %d"), dwError);
	}

	va_list argptr;
	va_start(argptr, format);

	_tprintf(_T("ERROR %d: %s -- "), dwError, win32Msg);
	_vtprintf(format, argptr);
	_tprintf(_T("\n"));

	::LocalFree(win32Msg);
}

void Logger::Debug1(const TCHAR* format, ...)
{   
	va_list argptr;
	va_start(argptr, format);

	_tprintf(_T("DEBUG 1: "));
	_vtprintf(format, argptr);
	_tprintf(_T("\n"));
}

#if DEBUG_LEVEL >= 2
void Logger::Debug2(const TCHAR* format, ...)
{   
	va_list argptr;
	va_start(argptr, format);

	_tprintf(_T("DEBUG 2: "));
	_vtprintf(format, argptr);
	_tprintf(_T("\n"));
}
#endif