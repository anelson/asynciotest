#include "StdAfx.h"
#include "TestServer.h"

#define COOKIE_FILE_WRITE	1
#define COOKIE_SOCKET_READ 2

TestServer::TestServer(Settings& settings) : IoController(settings)
{
	m_serverSocket = INVALID_SOCKET;
	m_clientSocket = INVALID_SOCKET;
	m_targetFile = INVALID_HANDLE_VALUE;
}

TestServer::~TestServer(void)
{
	Cleanup();
	if (m_serverSocket != INVALID_SOCKET) {
		::closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
	}
}


void TestServer::Run() {
	if (!BindSocket()) {
		return;
	}

	while (1) {
		if (IncludeNetworkTest()) {
			Logger::Debug1(_T("Waiting for client connection"));
			if (!AcceptConnection()) {
				return;
			}
		} else {
			//Pretend the network has already the entire chunk size, to facilitate
			//IO-only testing
			m_totalBytesRead = m_settings.getDataLength();
		}

		if (IncludeFileTest()) {
			if (!OpenOutputFile()) {
				return;
			}
		}

		Timer start = Timer::Now();

		if (IncludeNetworkTest()) {
			if (!PostInitialReads()) {
				return;
			}
		} else {
			if (!PostInitialWrites()) {
				return;
			}
		}

		Logger::Debug1(_T("Performing transfer"));
		do {
			AsyncIo* io = NULL;

			DWORD dwResult = GetNextCompletedOp(io);
			if (dwResult) {
				if (io != NULL) {
					if (io->m_dwCookie == COOKIE_SOCKET_READ) {
						Logger::ErrorWin32(dwResult, _T("Error reading from socket"));
					} else if (io->m_dwCookie == COOKIE_FILE_WRITE) {
						Logger::ErrorWin32(dwResult, _T("Error writing to target file"));
					} else {
						Logger::ErrorWin32(dwResult, _T("Error on unrecognized I/O operation"));
					}
				} else {
					Logger::ErrorWin32(dwResult, _T("Error getting next completed I/O operation"));
				}

				return;
			}

			if (io->m_dwCookie == COOKIE_SOCKET_READ) {
				if (!ProcessSocketRead(io)) {
					return;
				}
			} else if (io->m_dwCookie == COOKIE_FILE_WRITE) {
				if (!ProcessFileWrite(io)) {
					return;
				}
			} else {
				Logger::Error(_T("Unrecognized IO cookie %d"), io->m_dwCookie);
				return;
			}
		} while (!IsSocketDone() || !IsFileDone());

		Timer finish = Timer::Now();

		_tprintf(_T("Transfer complete\n"));
		_tprintf(_T("Total transfer wall clock time: %0.2lf seconds\n"), (finish - start).Seconds());
		if (IncludeNetworkTest()) {
			_tprintf(_T("Read %0.2lf 10^6 bytes\n"), 
				static_cast<double>(m_totalBytesRead) / 1000000);
		}
		if (IncludeFileTest()) {
			_tprintf(_T("Wrote %0.2lf 10^6 bytes\n"),
				static_cast<double>(m_totalBytesWritten) / 1000000);
		}
		_tprintf(_T("Total throughput: %0.2lf 10^6 Bytes/second (%0.2lf 10^6 bits/second)\n"),
			(static_cast<double>(MAX(m_totalBytesRead, m_totalBytesWritten)) / 1000000) / (finish - start).Seconds(),
			(static_cast<double>(MAX(m_totalBytesRead, m_totalBytesWritten)) / 1000000 * 8) / (finish - start).Seconds());


		Cleanup();

		if (!IncludeNetworkTest()) {
			//File system only test; only run one iteration
			break;
		}
	}
}

bool TestServer::BindSocket() {
	m_serverSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_serverSocket == INVALID_SOCKET) {
		Logger::ErrorWin32(::WSAGetLastError(), _T("Error creating server socket"));
		return false;
	}

	sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = ::htons(m_settings.getPort());

	if (::bind(m_serverSocket, reinterpret_cast<sockaddr*>(&sa), sizeof(sa))) {
		Logger::ErrorWin32(::WSAGetLastError(), 
			_T("Error binding server socket to port %d"),
			m_settings.getPort());
		return false;
	}

	if (::listen(m_serverSocket, 1)) {
		Logger::ErrorWin32(::WSAGetLastError(), _T("Error listening on server socket"));
		return false;
	}

	return true;
}

bool TestServer::AcceptConnection() {
	sockaddr_in sa = {0};
	int len = sizeof(sa);
	m_clientSocket = ::accept(m_serverSocket, reinterpret_cast<sockaddr*>(&sa), &len);
	if (m_clientSocket == INVALID_SOCKET) {
		Logger::ErrorWin32(::WSAGetLastError(), _T("Error accepting connection on server socket"));
		return false;
	}

	char* remote_host = ::inet_ntoa(sa.sin_addr);
#ifdef _UNICODE
	Logger::Debug1(_T("Got connection from host '%S'"), remote_host);
#else
	Logger::Debug1(_T("Got connection from host '%s'"), remote_host);
#endif

	if (!SetSocketBufSizes(m_clientSocket)) {
		return false;
	}

	if (!m_iocp.AssociateHandle(reinterpret_cast<HANDLE>(m_clientSocket))) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to associate client socket with IOCP"));
		return false;
	}

	return true;
}

bool TestServer::OpenOutputFile() {
	m_targetFile = ::CreateFile(_T("outfile.dat"), 
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_FLAG_OVERLAPPED,
		NULL);
	if (m_targetFile == INVALID_HANDLE_VALUE) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to open target file '%s'"), m_settings.getSourceFile().c_str());
		return false;
	}

	if (!m_iocp.AssociateHandle(m_targetFile)) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to associate output file with IOCP"));
		return false;
	}

	m_nextWriteOffset = 0;

	return true;
}

bool TestServer::PostInitialReads() {
	Logger::Debug1(_T("Positing initial socket reads"));
	while (m_outstandingReads < m_settings.getOpLength() && !IsSocketDone()) {
		if (!PostSocketRead()) {
			return false;
		}
	}

	return true;
}

bool TestServer::PostInitialWrites() {
	Logger::Debug1(_T("Positing initial file writes"));
	while (m_outstandingWrites < m_settings.getOpLength() && !IsFileDone()) {
		AsyncIo* io = new AsyncIo(m_settings.getChunkSize());
		io->m_dwBufLength = m_settings.getChunkSize();
		io->m_dwBytesXfered = io->m_dwBufLength;
		for (DWORD idx = 0; idx < io->m_dwBufSize; idx++) {
			io->m_buffer[idx] = static_cast<BYTE>(idx);
		}

		if (!PostFileWrite(io)) {
			delete io;
			return false;
		}
	}

	return true;
}

bool TestServer::ProcessSocketRead(AsyncIo* io) {
	//Write to file
	m_outstandingReads--;
	m_totalBytesRead += io->m_dwBytesXfered;

	Logger::Debug2(_T("Finished socket read; %d bytes read now; %I64d total"), io->m_dwBytesXfered, m_totalBytesRead);

	if (io->m_dwBytesXfered == 0) {
		//zero-byte read indicates the client has closed the connection
		if (m_clientSocket != INVALID_SOCKET) {
			Logger::Debug1(_T("Client transfer complete; closing connection to client"));
			::closesocket(m_clientSocket);
			m_clientSocket = INVALID_SOCKET;
		}

		delete io;
	} else {
		if (!PostSocketRead()) {
			delete io;
			return false;
		}

		if (IncludeFileTest()) {
			//Write what was just read, to the file
			if (!PostFileWrite(io)) {
				delete io;
				return false;
			}
		} else {
			//Drop this buffer on the floor
			delete io;
		}
	}

	return true;
}

bool TestServer::ProcessFileWrite(AsyncIo* io) {
	m_outstandingWrites--;
	m_totalBytesWritten += io->m_dwBytesXfered;

	Logger::Debug2(_T("Finished file write; %d bytes written now; %I64d total"), 
		io->m_dwBytesXfered,
		m_totalBytesWritten);

	if (!IncludeNetworkTest()) {
		//This is an IO-only test, so writes aren't posted as a result of socket
		//reads.  Post another write.
		if (!IsFileDone()) {
			if (!PostFileWrite(io)) {
				delete io;
				return false;
			}
		} else {
			delete io;
		}
	} else {
		//A socket read will post the next write; nothing else to do with
		//this IO object
		delete io;
	}

	return true;
}

bool TestServer::PostSocketRead() {
	Logger::Debug2(_T("Posting socket read"));

	AsyncIo* io = new AsyncIo(m_settings.getChunkSize());
	io->m_dwCookie = COOKIE_SOCKET_READ;
	io->m_dwBufLength = m_settings.getChunkSize();

	DWORD dwFlags = 0;
	if (::WSARecv(m_clientSocket, *io, 1, &io->m_dwBytesXfered, &dwFlags, *io, NULL) == SOCKET_ERROR &&
		::WSAGetLastError() != ERROR_IO_PENDING) {
		delete io;
		Logger::ErrorWin32(::WSAGetLastError(), 
			_T("Error reading data from remote host"));
		return false;
	}

	m_outstandingReads++;

	Logger::Debug2(_T("Read operation posted"));

	return true;
}

bool TestServer::PostFileWrite(AsyncIo* io) {
	io->m_dwCookie = COOKIE_FILE_WRITE;
	io->m_dwBufLength = io->m_dwBytesXfered;
	ULARGE_INTEGER offset;
	offset.QuadPart = m_nextWriteOffset;
	io->m_ol.Offset = offset.LowPart;
	io->m_ol.OffsetHigh = offset.HighPart;

	if (!::WriteFile(m_targetFile, 
		io->m_buffer,
		io->m_dwBufLength,
		&io->m_dwBytesXfered,
		*io) &&
		::GetLastError() != ERROR_IO_PENDING) {
		Logger::ErrorWin32(::GetLastError(), _T("Error posting write to target file"));
		delete io;
		return false;
	}

	m_outstandingWrites++;
	m_nextWriteOffset += io->m_dwBufLength;

	return true;
}

void TestServer::Cleanup() {
	Logger::Debug1(_T("Transfer complete; cleaning up"));

	if (m_clientSocket != INVALID_SOCKET) {
		::closesocket(m_clientSocket);
		m_clientSocket = INVALID_SOCKET;
	}
	if (m_targetFile != INVALID_HANDLE_VALUE) {
		::CloseHandle(m_targetFile);
		m_targetFile = INVALID_HANDLE_VALUE;
	}
}
