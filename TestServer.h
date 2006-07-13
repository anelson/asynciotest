#pragma once

#include "IoController.h"
#include "Settings.h"

class TestServer : public IoController
{
public:
	TestServer(Settings& settings);
	virtual ~TestServer(void);

	virtual void Run();

private:

	SOCKET m_serverSocket;
	SOCKET m_clientSocket;
	HANDLE m_targetFile;
	DWORD m_dwNextWriteOffset;

	bool BindSocket();
	bool AcceptConnection();
	bool OpenOutputFile();
	bool PostInitialReads();
	bool PostInitialWrites();
	bool ProcessSocketRead(AsyncIo* io);
	bool ProcessFileWrite(AsyncIo* io);
	bool PostSocketRead();
	bool PostFileWrite(AsyncIo* io);
	void Cleanup();

	bool IsSocketDone() {
		return !IncludeNetworkTest() || (m_clientSocket == INVALID_SOCKET);
	}
	bool IsFileDone() {
		return !IncludeFileTest() || (m_dwTotalBytesWritten >= m_dwTotalBytesRead);
	}
};
