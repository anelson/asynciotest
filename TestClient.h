#pragma once

#include "IoController.h"
#include "Settings.h"

class TestClient : public IoController
{
public:
	TestClient(Settings& settings);
	virtual ~TestClient(void);

	virtual void Run();

private:
	SOCKET m_socket;
	HANDLE m_file;
	DWORD m_dwFileSize;
	DWORD m_dwNextReadOffset;
	DWORD m_dwTotalBytesQueuedForWrite;
	LPFN_TRANSMITPACKETS m_pfnTransmitPackets;

	bool OpenSourceFile();
	bool ConnectToServer();

	bool PostFileRead();
	bool PostTransmitFile();
	bool PostSocketWrite(AsyncIo* srcIo);
	bool PostInitialFileReads();
	bool PostInitialSocketWrites();

	bool ProcessFileRead(AsyncIo* io);
	bool ProcessSocketWrite(AsyncIo* io);
	bool ProcessTransmitFile(AsyncIo* io);

	bool GetTransmitPacketsPointer();

	bool IsFileEof() {
		return !IncludeFileTest() || (m_dwNextReadOffset >= m_dwFileSize);
	}
	bool IsSocketDone() {
		return !IncludeNetworkTest() || (m_dwTotalBytesWritten >= m_dwFileSize);
	}

	bool UseTransmitFile() {
		return IncludeFileTest() && m_settings.getUseTransmitFunc();
	}

	bool UseTransmitPackets() {
		return !IncludeFileTest() && m_settings.getUseTransmitFunc();
	}
};
