// AsyncIoTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Settings.h"
#include "TestClient.h"
#include "TestServer.h"


int _tmain(int argc, _TCHAR* argv[])
{
	WSADATA wsa;
	DWORD err = ::WSAStartup(MAKEWORD(2,2), &wsa);
	if (err) {
		Logger::ErrorWin32(err, _T("Error initializing Winsock"));
		return -1;
	}

	Settings settings;

	if (!settings.ParseCommandLine(argc, argv)) {
		Settings::PrintUsage();
		return -1;
	}

	settings.DumpOptions();

	if (settings.getMode() == Client) {
		TestClient client(settings);
		client.Run();
	} else {
		TestServer server(settings);
		server.Run();
	}

	::WSACleanup();

#ifdef _DEBUG
	Logger::Debug1(_T("Dumping heap leaks"));
	::_CrtDumpMemoryLeaks();
#endif
}

