#pragma once

#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <winioctl.h>

#include <vector>

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
	unsigned __int64 m_fileSize;
	unsigned __int64 m_nextReadOffset;
	unsigned __int64 m_totalBytesQueuedForWrite;
	LPFN_TRANSMITPACKETS m_pfnTransmitPackets;

	// Snapshot-specific stuff
	CComPtr<IVssBackupComponents> m_components;
	GUID m_snapshotSetId;

	// Snapshot or raw disk specific stuff
	VOLUME_BITMAP_BUFFER* m_srcVolumeBitmap;
	unsigned __int64 m_totalClusters, m_freeClusters, m_bytesPerCluster;
	std::vector<unsigned __int64> m_allocatedChunks;

	bool OpenSourceFile();
	bool OpenSourceDisk(const tstring& volumeName);
	bool TakeDiskSnapshot();
	bool DeleteDiskSnapshot();
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
	bool GetVolumeBitmap(const tstring& volumeName);

	bool IsFileEof() {
		if (UseFileForFileTest()) {
			return !IncludeFileTest() || (m_nextReadOffset >= m_fileSize);
		} else {
			return m_allocatedChunks.size() == 0;
		}
	}
	bool IsSocketDone() {
		return !IncludeNetworkTest() || (m_totalBytesWritten >= m_fileSize);
	}

	bool UseTransmitFile() {
		return IncludeFileTest() && m_settings.getUseTransmitFunc();
	}

	bool UseTransmitPackets() {
		return !IncludeFileTest() && m_settings.getUseTransmitFunc();
	}
};
