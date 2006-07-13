#pragma once

#include "AsyncIo.h"
#include "Settings.h"
#include "IoCompletionPort.h"

#define MAX(a,b) ((a) >= (b) ? (a) : (b))

class IoController
{
public:
	IoController(Settings& settings);
	virtual ~IoController(void);

	virtual DWORD GetNextCompletedOp(AsyncIo*& pio);

	virtual void Run() = 0;

protected:
	Settings& m_settings;
	IoCompletionPort m_iocp;
	
	DWORD m_dwTotalBytesWritten;
	DWORD m_dwTotalBytesRead;
	int m_outstandingReads;
	int m_outstandingWrites;

	bool IncludeFileTest() {
		return m_settings.getOperation() != Network;
	}

	bool IncludeNetworkTest() {
		return m_settings.getOperation() != Disk;
	}

	bool SetSocketBufSizes(SOCKET sock);
};
