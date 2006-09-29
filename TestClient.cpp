#include "StdAfx.h"

#include <stdio.h>

#include <algorithm>

#include "TestClient.h"

typedef struct _RTL_BITMAP {
    ULONG SizeOfBitMap;                     // Number of bits in bit map
    PULONG Buffer;                          // Pointer to the bit map itself
} RTL_BITMAP, *PRTL_BITMAP;

extern "C" {
VOID
WINAPI
RtlInitializeBitMap (
    PRTL_BITMAP BitMapHeader,
    PULONG BitMapBuffer,
    ULONG SizeOfBitMap
    );

BOOLEAN
WINAPI
RtlAreBitsClear (
    PRTL_BITMAP BitMapHeader,
    ULONG StartingIndex,
    ULONG Length
    );
}


#define COOKIE_FILE_READ	1
#define COOKIE_SOCKET_WRITE 2
#define COOKIE_TRANSMIT_FILE 3

// Stolen from Wangdera Hobocopy, http://wangdera.sourceforge.net/
#define CHECK_HRESULT(funcname, x) \
{ \
	Logger::Debug2(_T("Calling %s"), funcname); \
	HRESULT ckhr = ((x)); \
	if (FAILED(ckhr)) { \
		Logger::ErrorWin32(ckhr, _T("%s failed"), funcname); \
		return false; \
	} \
}



TestClient::TestClient(Settings& settings) : IoController(settings)
{
	m_file = INVALID_HANDLE_VALUE;
	m_socket = 0;
	m_fileSize = 0;
	m_nextReadOffset = 0;
	m_totalBytesQueuedForWrite = 0;
	m_pfnTransmitPackets = NULL;
	m_srcVolumeBitmap = NULL;
	m_totalClusters = m_freeClusters = m_bytesPerCluster = 0;
}

TestClient::~TestClient(void)
{
	if (m_socket != INVALID_SOCKET) {
		::closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}

	if (m_file != INVALID_HANDLE_VALUE) {
		::CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
	}

	if (IncludeFileTest() && UseSnapshotForFileTest()) {
		DeleteDiskSnapshot();
	}

	if (m_srcVolumeBitmap) {
		delete[] m_srcVolumeBitmap;
		m_srcVolumeBitmap = NULL;
	}
}


void TestClient::Run() {
	Logger::Debug2(_T("Starting up..."));

	if (IncludeFileTest()) {
		if (UseSnapshotForFileTest()) {
			if (!TakeDiskSnapshot()) {
				return;
			}
		} else if (UseRawDiskForFileTest()) {
			if (!OpenSourceDisk(m_settings.getSourceFile())) {
				return;
			}
		} else {
			if (!OpenSourceFile()) {
				return;
			}
		}
	} else {
		m_fileSize = m_settings.getDataLength();
	}

	if (IncludeNetworkTest()) {
		if (!ConnectToServer()) {
			return;
		}

		if (UseTransmitPackets()) {
			if (!GetTransmitPacketsPointer()) {
				return;
			}
		}
	}


	Timer start = Timer::Now();

	//Post reads to the source file
	if (IncludeFileTest()) {
		//Unless using TransmitFile, post some initial file reads
		if (!UseTransmitFile()) {
			if (!PostInitialFileReads()) {
				return;
			}
		} else {
			if (!PostTransmitFile()) {
				return;
			}
		}
	} else {
		if (!PostInitialSocketWrites()) {
			return;
		}
	}

	Logger::Debug1(_T("Commencing transfer"));

	do {
		AsyncIo* io = NULL;

		Logger::Debug2(_T("Getting next completed I/O operation"));

		DWORD dwResult = GetNextCompletedOp(io);
		if (dwResult) {
			if (io != NULL) {
				if (io->m_dwCookie == COOKIE_FILE_READ) {
					Logger::ErrorWin32(dwResult, _T("Error reading from source file"));
				} else if (io->m_dwCookie == COOKIE_SOCKET_WRITE) {
					Logger::ErrorWin32(dwResult, _T("Error writing to host socket"));
				} else if (io->m_dwCookie == COOKIE_TRANSMIT_FILE) {
					Logger::ErrorWin32(dwResult, _T("Error in TransmitFile function"));
				} else {
					Logger::ErrorWin32(dwResult, _T("Error on unrecognized I/O operation"));
				}
			} else {
				Logger::ErrorWin32(dwResult, _T("Error getting next completed I/O operation"));
			}

			return;
		}

		if (io->m_dwCookie == COOKIE_FILE_READ) {
			if (!ProcessFileRead(io)) {
				return;
			}
		} else if (io->m_dwCookie == COOKIE_SOCKET_WRITE) {
			if (!ProcessSocketWrite(io)) {
				return;
			}
		} else if (io->m_dwCookie == COOKIE_TRANSMIT_FILE) {
			if (!ProcessTransmitFile(io)) {
				return;
			}
		} else {
			Logger::Error(_T("Unrecognized IO cookie %d"), io->m_dwCookie);
			return;
		}
	} while (!IsFileEof() || !IsSocketDone());

	Timer finish = Timer::Now();

	_tprintf(_T("Transfer complete\n"));
	_tprintf(_T("Total transfer wall clock time: %0.2lf seconds\n"), (finish - start).Seconds());
	if (IncludeFileTest()) {
		_tprintf(_T("Read %0.2lf 10^6 bytes\n"), 
			static_cast<double>(m_totalBytesRead) / 1000000);
	}
	if (IncludeNetworkTest()) {
		_tprintf(_T("Sent %0.2lf 10^6 bytes\n"),
			static_cast<double>(m_totalBytesWritten) / 1000000);
	}
	_tprintf(_T("Total throughput: %0.2lf 10^6 Bytes/second (%0.2lf 10^6 bits/second)\n"),
		(static_cast<double>(MAX(m_totalBytesRead, m_totalBytesWritten)) / 1000000) / (finish - start).Seconds(),
		(static_cast<double>(MAX(m_totalBytesRead, m_totalBytesWritten)) / 1000000 * 8) / (finish - start).Seconds());
}

bool TestClient::OpenSourceFile() {
	Logger::Debug1(_T("Opening source file"));

	m_file = ::CreateFile(m_settings.getSourceFile().c_str(), 
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (m_file == INVALID_HANDLE_VALUE) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to open source file '%s'"), m_settings.getSourceFile().c_str());
		return false;
	}

	DWORD sizeHigh = 0;
	m_fileSize = ::GetFileSize(m_file, &sizeHigh);
	if (m_fileSize == INVALID_FILE_SIZE) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to get size of source file '%s'"), m_settings.getSourceFile().c_str());
		return false;
	}

	if (sizeHigh) {
		Logger::Error(_T("The source file '%s' is larger than 4GB, and cannot be used for this test"),
			m_settings.getSourceFile().c_str());
		return false;
	}

	//Associate the file with the IOCP
	if (!m_iocp.AssociateHandle(m_file)) {
		return false;
	}

	Logger::Debug1(_T("Source file %s opened successfully"), 
		m_settings.getSourceFile().c_str());

	return true;
}

bool TestClient::OpenSourceDisk(const tstring& volumeName) {
	m_file = ::CreateFile(volumeName.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ|FILE_SHARE_WRITE,
                                    NULL,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
                                    NULL);
	if (m_file == INVALID_HANDLE_VALUE) {
		Logger::ErrorWin32(::GetLastError(),
			_T("Failed to open source volume '%s'"),
			volumeName.c_str());
		return false;
	}
	
	NTFS_VOLUME_DATA_BUFFER ntfsInfo = {0};
    DWORD cbDontCare;
	DWORD status = ::DeviceIoControl (
                    m_file,
                    FSCTL_GET_NTFS_VOLUME_DATA,
                    NULL,
                    0,
                    &ntfsInfo,
                    sizeof(NTFS_VOLUME_DATA_BUFFER),
                    &cbDontCare,
                    NULL);
    if (!status)
    {
		Logger::ErrorWin32(::GetLastError(), 
			_T("Failed to get size of source volume '%s'"), 
			volumeName.c_str());
		return false;
    }

	m_totalClusters = ntfsInfo.TotalClusters.QuadPart;
	m_freeClusters = ntfsInfo.FreeClusters.QuadPart;
	m_bytesPerCluster = ntfsInfo.BytesPerCluster;

	//Technically the 'size' of this file is the total clusters times bytes
	//per cluster, but since we're only transmitting the allocated clusters,
	//report the size as allocated clsuters only
	m_fileSize = (m_totalClusters-m_freeClusters) * m_bytesPerCluster;

	if (m_settings.getChunkSize() < ntfsInfo.BytesPerCluster ||
		(m_settings.getChunkSize() % ntfsInfo.BytesPerCluster) != 0) {
		Logger::Error(_T("The chunk size must be an even multiple of the volume cluster size, which is %d bytes"),
			ntfsInfo.BytesPerCluster);
	}

	// Get the volume bitmap
	if (!GetVolumeBitmap(volumeName)) {
		return false;
	}
	
	//Associate the file with the IOCP
	if (!m_iocp.AssociateHandle(m_file)) {
		return false;
	}

	Logger::Debug1(_T("Source volume snapshot '%s' opened successfully"),
		volumeName.c_str());

	return true;
}

bool TestClient::TakeDiskSnapshot() {
    CHECK_HRESULT(_T("CoInitialize"), ::CoInitialize(NULL)); 
    CHECK_HRESULT(_T("CoInitializeSecurity"), 
        ::CoInitializeSecurity(
        NULL, 
        -1, 
        NULL, 
        NULL, 
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY, 
        RPC_C_IMP_LEVEL_IDENTIFY, 
        NULL, 
        EOAC_NONE, 
        NULL)); 

	Logger::Debug1(_T("Preparing to take snapshot of volume '%s'"),
		m_settings.getSourceFile().c_str());

	CHECK_HRESULT(_T("CreateVssBackupComponents"), ::CreateVssBackupComponents(&m_components));

	CHECK_HRESULT(_T("m_components->InitializeForBackup"), m_components->InitializeForBackup());

    CComPtr<IVssAsync> pWriterMetadataStatus; 

    CHECK_HRESULT(_T("m_components->GatherWriterMetadata"), m_components->GatherWriterMetadata(&pWriterMetadataStatus)); 

    CHECK_HRESULT(_T("pWriterMetadataStatus->Wait"), pWriterMetadataStatus->Wait()); 

    HRESULT hrGatherStatus; 
    CHECK_HRESULT(_T("pWriterMetadataStatus->QueryStatus"), pWriterMetadataStatus->QueryStatus(&hrGatherStatus, NULL)); 

    if (hrGatherStatus == VSS_S_ASYNC_CANCELLED)
    {
		Logger::Error(_T("GatherWriterMetadata was cancelled"));
		return false;
    }

    UINT cWriters; 
    CHECK_HRESULT(_T("m_components->GetWriterMetadataCount"), m_components->GetWriterMetadataCount(&cWriters)); 

    for (UINT i = 0; i < cWriters; ++i)
    {
        CComPtr<IVssExamineWriterMetadata> pExamineWriterMetadata; 
        GUID id; 
        CHECK_HRESULT(_T("m_components->GetWriterMetadata"), m_components->GetWriterMetadata(i, &id, &pExamineWriterMetadata)); 
        GUID idInstance; 
        GUID idWriter; 
        BSTR bstrWriterName;
        VSS_USAGE_TYPE usage; 
        VSS_SOURCE_TYPE source; 
        CHECK_HRESULT(_T("pExamineWriterMetadata->GetIdentity"), pExamineWriterMetadata->GetIdentity(&idInstance, &idWriter, &bstrWriterName, &usage, &source)); 

        CComBSTR writerName(bstrWriterName); 
		Logger::Debug2(_T("Writer %d named %s"), i, bstrWriterName);
    }

    CHECK_HRESULT(_T("m_components->StartSnapshotSet"), m_components->StartSnapshotSet(&m_snapshotSetId));

    WCHAR wszVolumePathName[MAX_PATH]; 
    BOOL bWorked = ::GetVolumePathName(m_settings.getSourceFile().c_str(), wszVolumePathName, MAX_PATH); 

    if (!bWorked)
    {
        DWORD error = ::GetLastError(); 
		Logger::ErrorWin32(error, _T("GetVolumePathName for '%s' failed"),
			m_settings.getSourceFile().c_str());

		return false;
    }

    GUID snapshotId; 
    CHECK_HRESULT(_T("m_components->AddToSnapshotSet"), m_components->AddToSnapshotSet(wszVolumePathName, GUID_NULL, &snapshotId)); 

    CHECK_HRESULT(_T("m_components->SetBackupState"), m_components->SetBackupState(FALSE, FALSE, VSS_BT_FULL, FALSE)); 

    CComPtr<IVssAsync> pPrepareForBackupResults; 
    CHECK_HRESULT(_T("m_components->PrepareForBackup"), m_components->PrepareForBackup(&pPrepareForBackupResults)); 

    CHECK_HRESULT(_T("pPrepareForBackupResults->Wait"), pPrepareForBackupResults->Wait()); 

    HRESULT hrPrepareForBackupResults; 
    CHECK_HRESULT(_T("pPrepareForBackupResults->QueryStatus"), pPrepareForBackupResults->QueryStatus(&hrPrepareForBackupResults, NULL)); 

    if (hrPrepareForBackupResults != VSS_S_ASYNC_FINISHED)
    {
		Logger::ErrorWin32(hrPrepareForBackupResults, _T("Prepare for backup failed.")); 
    }


	Logger::Debug1(_T("Taking snapshot of volume '%s'"),
		m_settings.getSourceFile().c_str());

    CComPtr<IVssAsync> pDoSnapshotSetResults;
    CHECK_HRESULT(_T("m_components->DoSnapshotSet"), m_components->DoSnapshotSet(&pDoSnapshotSetResults)); 

    CHECK_HRESULT(_T("pDoSnapshotSetResults->Wait"), pDoSnapshotSetResults->Wait());

    HRESULT hrDoSnapshotSetResults; 
    CHECK_HRESULT(_T("pDoSnapshotSetResults->QueryStatus"), pDoSnapshotSetResults->QueryStatus(&hrDoSnapshotSetResults, NULL)); 

    if (hrDoSnapshotSetResults != VSS_S_ASYNC_FINISHED)
    {
		Logger::ErrorWin32(hrDoSnapshotSetResults, _T("DoSnapshotSet failed.")); 
    }

    VSS_SNAPSHOT_PROP snapshotProperties; 
    CHECK_HRESULT(_T("m_components->GetSnapshotProperties"), m_components->GetSnapshotProperties(snapshotId, &snapshotProperties));

	Logger::Debug1(_T("Snapshot of volume '%s' complete.  Snapshot device is '%s'"),
		m_settings.getSourceFile().c_str(),
		snapshotProperties.m_pwszSnapshotDeviceObject);

	return OpenSourceDisk(snapshotProperties.m_pwszSnapshotDeviceObject);
}

bool TestClient::DeleteDiskSnapshot() {
	if (!m_components) {
		return true;
	}
	
    CComPtr<IVssAsync> pBackupCompleteResults; 
	HRESULT hr = m_components->BackupComplete(&pBackupCompleteResults);
	if (FAILED(hr)) {
		Logger::ErrorWin32(hr, TEXT("m_components->BackupComplete failed")); 
	}

    HRESULT hrBackupCompleteResults; 
    hr = pBackupCompleteResults->QueryStatus(&hrBackupCompleteResults, NULL); 
	if (FAILED(hr)) {
		Logger::ErrorWin32(hr, TEXT("pBackupCompleteResults->QueryStatus failed")); 
	} else if (hrBackupCompleteResults != VSS_S_ASYNC_FINISHED)
    {
		Logger::ErrorWin32(hrBackupCompleteResults, TEXT("Completion of backup failed.")); 
    }

	LONG ldontcare;
	VSS_ID pdontcare;
	m_components->DeleteSnapshots(m_snapshotSetId, VSS_OBJECT_SNAPSHOT_SET, TRUE, &ldontcare, &pdontcare);

	m_components.Release();

	return true;
}

bool TestClient::ConnectToServer() {
	Logger::Debug1(_T("Attempting to connect to server"));

#ifdef _UNICODE
	CHAR hostname[255];
	int numBytes = ::WideCharToMultiByte(0, 
		CP_ACP, 
		m_settings.getTargetHost().c_str(),
		static_cast<int>(m_settings.getTargetHost().length()),
		hostname,
		254,
		" ",
		NULL);
	hostname[numBytes] = '\0';
#endif

	CHAR port[NI_MAXSERV];

	::_snprintf(port, NI_MAXSERV, "%u", m_settings.getPort());

	addrinfo* paddr = NULL;
	addrinfo hint = {0};
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;
	
#ifdef _UNICODE
	if (::getaddrinfo(hostname, port, &hint, &paddr)) {
#else
	if (::getaddrinfo(m_settings.getTargetHost().c_str(), port, &hint, &paddr)) {
#endif
	Logger::ErrorWin32(::WSAGetLastError(), _T("Unable to resolve target hostname '%s'"), m_settings.getTargetHost().c_str());
		return false;
	}

	m_socket = ::socket(paddr->ai_family, paddr->ai_socktype, paddr->ai_protocol);
	if (m_socket == INVALID_SOCKET) {
		::freeaddrinfo(paddr);
		Logger::ErrorWin32(::WSAGetLastError(), _T("Unable to create socket"));
		return false;
	}

	if (!SetSocketBufSizes(m_socket)) {
		return false;
	}

	if (::connect(m_socket, paddr->ai_addr, static_cast<int>(paddr->ai_addrlen))) {
		::freeaddrinfo(paddr);
		Logger::ErrorWin32(::WSAGetLastError(), _T("Unable to connect to server '%s'"), m_settings.getTargetHost().c_str());
		::closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		return false;
	}
	
	::freeaddrinfo(paddr); paddr = NULL;

	//Associate the socket with the IOCP so it can be used for overlapped IO
	if (!m_iocp.AssociateHandle(reinterpret_cast<HANDLE>(m_socket))) {
		return false;
	}

	Logger::Debug1(_T("Connection to '%s' established"), m_settings.getTargetHost().c_str());
    
	return true;
}


bool TestClient::PostFileRead() {
	Logger::Debug2(_T("Posting file read at offset %I64d"), m_nextReadOffset);

	AsyncIo* io = new AsyncIo(m_nextReadOffset, m_settings.getChunkSize());
	io->m_dwCookie = COOKIE_FILE_READ;
	io->m_dwBufLength = m_settings.getChunkSize();

	if (!::ReadFile(m_file, io->m_buffer, io->m_dwBufLength, &io->m_dwBytesXfered, *io) &&
		::GetLastError() != ERROR_IO_PENDING) {
		Logger::ErrorWin32(::GetLastError(), _T("Error reading from source file"));
		delete io;
		return false;
	}

	if (ReadingFromDisk()) {
		//Get the next read offset from the pre-computed list of allocated chunks
		if (m_allocatedChunks.size()) {
			m_nextReadOffset = m_allocatedChunks.back();
			m_allocatedChunks.pop_back();
		} else {
			Logger::Debug2(_T("No more allocated clusters to read; using current value"));
		}
	} else {
		m_nextReadOffset += m_settings.getChunkSize();
	}
	m_outstandingReads++;

	Logger::Debug2(_T("Read operation posted"));

	return true;
}

bool TestClient::PostTransmitFile() {
	Logger::Debug1(_T("Posting TransmitFile"));

	AsyncIo* io = new AsyncIo();
	io->m_dwCookie = COOKIE_TRANSMIT_FILE;

	if (!::TransmitFile(m_socket,
		m_file,
		0,
		m_settings.getChunkSize(),
		*io,
		NULL,
		TF_USE_KERNEL_APC) &&
		::GetLastError() != ERROR_IO_PENDING) {
		Logger::ErrorWin32(::GetLastError(), _T("Call to TransmitFile failed"));
		return false;
	}

	m_outstandingReads++;
	m_outstandingWrites++;

	Logger::Debug1(_T("TransmitFile operation posted"));

	return true;
}

bool TestClient::PostSocketWrite(AsyncIo* srcIo) {
	Logger::Debug2(_T("Posting socket write of %d bytes"), srcIo->m_dwBytesXfered);

	srcIo->m_dwBufLength = srcIo->m_dwBytesXfered;
	srcIo->m_dwCookie = COOKIE_SOCKET_WRITE;

	if (UseTransmitPackets()) {
		if (!m_pfnTransmitPackets(m_socket, 
			*srcIo, 
			1, 
			m_settings.getChunkSize(),
			*srcIo, 
			TF_USE_KERNEL_APC) &&
			::WSAGetLastError() != ERROR_IO_PENDING) {
				Logger::ErrorWin32(::WSAGetLastError(), 
					_T("Error sending data to remote host with TransmitPackets"));
				return false;
		}
	} else {
		if (::WSASend(m_socket, *srcIo, 1, &srcIo->m_dwBytesXfered, 0, *srcIo, NULL) &&
			::WSAGetLastError() != ERROR_IO_PENDING) {
				Logger::ErrorWin32(::WSAGetLastError(), 
					_T("Error sending data to remote host"));
				return false;
		}
	}

	m_outstandingWrites++;
	m_totalBytesQueuedForWrite += srcIo->m_dwBufLength;

	return true;
}

bool TestClient::ProcessFileRead(AsyncIo* io) {
	Logger::Debug2(_T("Processing completed file read: %d bytes read"),
		io->m_dwBytesXfered);
	//File read completed.  Update counters and send to socket
	m_outstandingReads--;
	m_totalBytesRead += io->m_dwBytesXfered;
	
	if (IncludeNetworkTest()) {
		//Write this back out to the socket
		io->m_dwCookie = COOKIE_SOCKET_WRITE;
		io->m_ol.Offset = 0;

		if (!PostSocketWrite(io)) {
			delete io;
			return false;
		}
	} else {
		//Drop it on the floor
		delete io;
		io = NULL;
	}

	//Post another read
	if (!IsFileEof()) {
		return PostFileRead();
	} else {
		return true;
	}
}

bool TestClient::ProcessSocketWrite(AsyncIo* io) {
	Logger::Debug2(_T("Processing completed socket write: %d bytes written"),
		io->m_dwBytesXfered);
	//Update counters
	m_totalBytesWritten += io->m_dwBytesXfered;
	m_outstandingWrites--;

	if (!IncludeFileTest() && !IsSocketDone() && m_totalBytesQueuedForWrite < m_fileSize) {
		//No file reads to prompt socket writes, so post another socket write now
		if (!PostSocketWrite(io)) {
			delete io;
			return false;
		}
	} else {
		//Another socket write will happen when a file read finishes
		delete io;
	}

	return true;
}

bool TestClient::ProcessTransmitFile(AsyncIo* io) {
	Logger::Debug1(_T("Processing completed TransmitFIle: %d bytes read and sent"),
		io->m_dwBytesXfered);

	m_outstandingReads--;
	m_outstandingWrites--;

	m_totalBytesRead = io->m_dwBytesXfered;
	m_totalBytesWritten = io->m_dwBytesXfered;
	m_nextReadOffset = m_fileSize;

	delete io;

	if (!IsSocketDone()) {
		Logger::Error(_T("After successful TransmitFile, transfer isn't complete"));
		return false;
	}

	return true;
}

bool TestClient::PostInitialFileReads() {
	Logger::Debug1(_T("Positing initial file reads"));
	while (m_outstandingReads < m_settings.getOpLength() && !IsFileEof()) {
		if (!PostFileRead()) {
			return false;
		}
	}

	return true;
}

bool TestClient::PostInitialSocketWrites() {
	Logger::Debug1(_T("Positing initial socket writes"));
	while (m_outstandingWrites < m_settings.getOpLength() && !IsSocketDone()) {
		AsyncIo* io = new AsyncIo(m_settings.getChunkSize());
		io->m_dwBufLength = m_settings.getChunkSize();
		io->m_dwBytesXfered = io->m_dwBufLength;
		for (DWORD idx = 0; idx < io->m_dwBufSize; idx++) {
			io->m_buffer[idx] = static_cast<BYTE>(idx);
		}

		if (!PostSocketWrite(io)) {
			delete io;
			return false;
		}
	}

	return true;
}

bool TestClient::GetTransmitPacketsPointer() {
	DWORD dwDontCare;
	GUID guidTransmitPackets = WSAID_TRANSMITPACKETS;

	DWORD dwErr = ::WSAIoctl(m_socket,
                 SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guidTransmitPackets,
                 sizeof(guidTransmitPackets),
                 &m_pfnTransmitPackets,
                 sizeof(m_pfnTransmitPackets),
                 &dwDontCare,
                 NULL,
                 NULL); 
	if (dwErr != 0) {
		Logger::ErrorWin32(::WSAGetLastError(), _T("Unable to get function pointer for TransmitPackets"));
		return false;
	}

	return true;
}

bool TestClient::GetVolumeBitmap(const tstring& volumeName) {
    STARTING_LCN_INPUT_BUFFER lcn;
    BOOL ok;
    DWORD cbDontCare;

	ULONG bitmapBufferSize = 65536;

	do {        
		if (m_srcVolumeBitmap) {
			delete[] m_srcVolumeBitmap;
		}

		m_srcVolumeBitmap = reinterpret_cast<VOLUME_BITMAP_BUFFER*>(new char[bitmapBufferSize]);

		lcn.StartingLcn.QuadPart = 0;
		ok = ::DeviceIoControl (
						m_file,
						FSCTL_GET_VOLUME_BITMAP,
						&lcn,
						sizeof(STARTING_LCN_INPUT_BUFFER),
						m_srcVolumeBitmap,
						bitmapBufferSize,
						&cbDontCare,
						NULL);

		if (!ok) {
			if (::GetLastError() == ERROR_MORE_DATA) {
				//Need a bigger buffer
				bitmapBufferSize <<= 1;
			} else {
				Logger::ErrorWin32(::GetLastError(),
					_T("Error getting volume bitmap for source volume '%s'"),
					volumeName.c_str());
				return false;
			}
		}
	} while (!ok);

#define BITS_IN_ULONG	((sizeof(ULONG)*8))

	if (m_srcVolumeBitmap->BitmapSize.QuadPart % BITS_IN_ULONG != 0) {
		m_srcVolumeBitmap->BitmapSize.QuadPart += BITS_IN_ULONG - (m_srcVolumeBitmap->BitmapSize.QuadPart % BITS_IN_ULONG);
	}

	if (m_srcVolumeBitmap->BitmapSize.HighPart) {
		Logger::Error(_T("Source volume bitmap for source volume '%s' is larger than 4Gbits, and cannot be used for this test"),
			volumeName.c_str());
		return false;
	}

	RTL_BITMAP bitmap = {0};
	::RtlInitializeBitMap (
				&bitmap,
				reinterpret_cast<ULONG*>(m_srcVolumeBitmap->Buffer),
				m_srcVolumeBitmap->BitmapSize.LowPart);

	//Build a list of allocated clusters from the volume bitmap
	unsigned __int64 totalChunks = (m_totalClusters * m_bytesPerCluster + (m_settings.getChunkSize()-1)) / m_settings.getChunkSize();

	for (unsigned __int64 chunk = 0; chunk < totalChunks; chunk++) {
		//Compute the range of clusters covered by this chunk, and check them for availability
		unsigned __int64 byteOffset = chunk * m_settings.getChunkSize();

		unsigned __int64 startCluster = byteOffset / m_bytesPerCluster;
		unsigned __int64 endCluster = (byteOffset + m_settings.getChunkSize()) / m_bytesPerCluster;

		ULONG bitOffset = static_cast<ULONG>(startCluster);
		ULONG bitCount = static_cast<ULONG>(endCluster - startCluster + 1);

		if (!::RtlAreBitsClear(&bitmap, bitOffset, bitCount)) {
			m_allocatedChunks.push_back(byteOffset);
		}
	}

	//Reverse the list, so it starts with lower numbers
	std::reverse(m_allocatedChunks.begin(), m_allocatedChunks.end());

	if (m_fileSize == 0) {
		m_fileSize = m_settings.getChunkSize();
		m_allocatedChunks.push_back(0);
	}

	m_nextReadOffset = m_allocatedChunks.back();
	m_allocatedChunks.pop_back();

	return true;
}
