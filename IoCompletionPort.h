#pragma once

class IoCompletionPort
{
public:
	IoCompletionPort(void);

	~IoCompletionPort(void);

	bool AssociateHandle(HANDLE handle, ULONG_PTR pCompletionKey = NULL);

	DWORD GetQueuedCompletionStatus(LPDWORD lpBytesTransferred, PULONG_PTR lpCompletionKey, LPOVERLAPPED* lpOverlapped);

private:
	HANDLE m_iocp;
};
