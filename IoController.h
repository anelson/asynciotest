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
	
	unsigned __int64 m_totalBytesWritten;
	unsigned __int64 m_totalBytesRead;
	int m_outstandingReads;
	int m_outstandingWrites;

	bool IncludeFileTest() {
		return m_settings.getOperation() != Network;
	}

	bool UseSnapshotForFileTest() {
		return m_settings.getDiskReadSource() == Snapshot;
	}

	bool UseRawDiskForFileTest() {
		return m_settings.getDiskReadSource() == RawDisk;
	}

	bool UseFileForFileTest() {
		return m_settings.getDiskReadSource() == File;
	}

	bool ReadingFromDisk() {
		return UseSnapshotForFileTest() || UseRawDiskForFileTest();
	}

	bool IncludeNetworkTest() {
		return m_settings.getOperation() != Disk;
	}

	bool SetSocketBufSizes(SOCKET sock);
};
