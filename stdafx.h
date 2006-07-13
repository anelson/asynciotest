// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <string>

#include <winsock2.h>
#include <mswsock.h>
#include <Ws2tcpip.h>

#if _UNICODE
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif


//If in debug mode, enable heap debugging
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

//As per http://support.microsoft.com/kb/q140858/, override operator new to use
//debug version so mem leak dumps show actual line number of allocation
#define MYDEBUG_NEW   new( _NORMAL_BLOCK, __FILE__, __LINE__)
// Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the
//allocations to be of _CLIENT_BLOCK type

#define new MYDEBUG_NEW
#endif