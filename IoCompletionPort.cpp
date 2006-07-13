#include "StdAfx.h"
#include "IoCompletionPort.h"
#include "Logger.h"

IoCompletionPort::IoCompletionPort(void)
{
	m_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	if (!m_iocp) {
		Logger::ErrorWin32(::GetLastError(), _T("Failed to create IOCP"));
	}
}

IoCompletionPort::~IoCompletionPort(void)
{
	if (m_iocp) {
		::CloseHandle(m_iocp);
		m_iocp = NULL;
	}
}

bool IoCompletionPort::AssociateHandle(HANDLE handle, ULONG_PTR pCompletionKey /* = NULL */) {
	if (!::CreateIoCompletionPort(handle, m_iocp, pCompletionKey, 0)) {
		Logger::ErrorWin32(::GetLastError(), _T("Unable to associate handle with IOCP"));
		return false;
	}

	return true;
}

DWORD IoCompletionPort::GetQueuedCompletionStatus(LPDWORD lpBytesTransferred, PULONG_PTR lpCompletionKey, LPOVERLAPPED* lpOverlapped) {
	BOOL result = ::GetQueuedCompletionStatus(m_iocp,
		lpBytesTransferred,
		lpCompletionKey,
		lpOverlapped,
		INFINITE);

	if (!result) {
		return ::GetLastError();
	}

	return 0;
}