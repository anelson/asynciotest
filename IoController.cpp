#include "StdAfx.h"
#include "IoController.h"

IoController::IoController(Settings& settings) : m_settings(settings)
{
	m_dwTotalBytesWritten = 0;
	m_dwTotalBytesRead = 0;
	m_outstandingReads = 0;
	m_outstandingWrites = 0;
}

IoController::~IoController(void)
{
}

DWORD IoController::GetNextCompletedOp(AsyncIo*& pio) {
	DWORD dwBytesTransferred;
	ULONG_PTR lpCompletionKey;
	DWORD dwResult = m_iocp.GetQueuedCompletionStatus(&dwBytesTransferred,
		&lpCompletionKey,
		reinterpret_cast<LPOVERLAPPED*>(&pio));

	if (dwResult == 0) {
		pio->OnIoComplete(dwBytesTransferred, lpCompletionKey);
	}


	return dwResult;
}

bool IoController::SetSocketBufSizes(SOCKET sock) {
	int buf = m_settings.getTcpBufSize();

	if (::setsockopt(sock, 
			SOL_SOCKET, 
			SO_SNDBUF, 
			reinterpret_cast<char*>(&buf), 
			sizeof(buf))) {
		Logger::ErrorWin32(::GetLastError(), 
			_T("Failed to set socket send buffer to %d"),
			buf);
		return false;
	}

	if (::setsockopt(sock, 
			SOL_SOCKET, 
			SO_RCVBUF, 
			reinterpret_cast<char*>(&buf), 
			sizeof(buf))) {
		Logger::ErrorWin32(::GetLastError(), 
			_T("Failed to set socket receive buffer to %d"),
			buf);
		return false;
	}

	return true;
}
