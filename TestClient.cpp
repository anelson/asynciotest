#include "StdAfx.h"
#include "TestClient.h"

#include <stdio.h>

#define COOKIE_FILE_READ	1
#define COOKIE_SOCKET_WRITE 2
#define COOKIE_TRANSMIT_FILE 3

TestClient::TestClient(Settings& settings) : IoController(settings)
{
	m_file = INVALID_HANDLE_VALUE;
	m_socket = 0;
	m_dwFileSize = 0;
	m_dwNextReadOffset = 0;
	m_dwTotalBytesQueuedForWrite = 0;
	m_pfnTransmitPackets = NULL;
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
}


void TestClient::Run() {
	Logger::Debug2(_T("Starting up..."));

	if (IncludeFileTest()) {
		if (!OpenSourceFile()) {
			return;
		}
	} else {
		m_dwFileSize = m_settings.getDataLength();
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
			static_cast<double>(m_dwTotalBytesRead) / 1000000);
	}
	if (IncludeNetworkTest()) {
		_tprintf(_T("Sent %0.2lf 10^6 bytes\n"),
			static_cast<double>(m_dwTotalBytesWritten) / 1000000);
	}
	_tprintf(_T("Total throughput: %0.2lf 10^6 Bytes/second (%0.2lf 10^6 bits/second)\n"),
		(static_cast<double>(MAX(m_dwTotalBytesRead, m_dwTotalBytesWritten)) / 1000000) / (finish - start).Seconds(),
		(static_cast<double>(MAX(m_dwTotalBytesRead, m_dwTotalBytesWritten)) / 1000000 * 8) / (finish - start).Seconds());
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

	m_dwFileSize = ::GetFileSize(m_file, NULL);
	if (m_dwFileSize == INVALID_FILE_SIZE) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to query size for source file '%s'"), m_settings.getSourceFile().c_str());
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
	Logger::Debug2(_T("Posting file read at offset %d"), m_dwNextReadOffset);

	AsyncIo* io = new AsyncIo(m_dwNextReadOffset, m_settings.getChunkSize());
	io->m_dwCookie = COOKIE_FILE_READ;
	io->m_dwBufLength = m_settings.getChunkSize();

	if (!::ReadFile(m_file, io->m_buffer, io->m_dwBufLength, &io->m_dwBytesXfered, *io) &&
		::GetLastError() != ERROR_IO_PENDING) {
		Logger::ErrorWin32(::GetLastError(), _T("Error reading from source file"));
		delete io;
		return false;
	}

	m_dwNextReadOffset += m_settings.getChunkSize();
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
	m_dwTotalBytesQueuedForWrite += srcIo->m_dwBufLength;

	return true;
}

bool TestClient::ProcessFileRead(AsyncIo* io) {
	Logger::Debug2(_T("Processing completed file read: %d bytes read"),
		io->m_dwBytesXfered);
	//File read completed.  Update counters and send to socket
	m_outstandingReads--;
	m_dwTotalBytesRead += io->m_dwBytesXfered;
	
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
	m_dwTotalBytesWritten += io->m_dwBytesXfered;
	m_outstandingWrites--;

	if (!IncludeFileTest() && !IsSocketDone() && m_dwTotalBytesQueuedForWrite < m_dwFileSize) {
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

	m_dwTotalBytesRead = io->m_dwBytesXfered;
	m_dwTotalBytesWritten = io->m_dwBytesXfered;
	m_dwNextReadOffset = m_dwFileSize;

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
