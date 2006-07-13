#pragma once

#include "Timer.h"

struct AsyncIo
{
	OVERLAPPED m_ol;

	HANDLE m_ioHandle;
	DWORD m_dwBytesXfered;

	DWORD m_dwBufSize;
	DWORD m_dwBufLength;
	BYTE* m_buffer;

	DWORD m_dwCookie;

	WSABUF m_wsa;

	TRANSMIT_PACKETS_ELEMENT m_tpe;

public:
	AsyncIo(void);
	AsyncIo(DWORD dwBufSize);
	AsyncIo(DWORD dwOffset, DWORD dwBufSize);

	~AsyncIo(void);

	bool AllocateBuffer(DWORD dwBufSize);
	void OnIoComplete(DWORD dwBytesTransferred, ULONG_PTR lpCompletionKey);

	operator LPOVERLAPPED () {
		return &m_ol;
	}

	operator LPWSABUF () {
		m_wsa.buf = reinterpret_cast<char*>(m_buffer);
		m_wsa.len = m_dwBufLength;

		return &m_wsa;
	}

	operator LPTRANSMIT_PACKETS_ELEMENT () {
		m_tpe.dwElFlags = TP_ELEMENT_MEMORY;
		m_tpe.pBuffer = m_buffer;
		m_tpe.cLength = m_dwBufLength;

		return &m_tpe;
	}

	void TakeBufferOwnership(AsyncIo& src) {
		if (m_buffer) {
			delete[] m_buffer;
		}

		m_buffer = src.m_buffer;
		m_dwBufSize = src.m_dwBufSize;
		src.m_buffer = NULL;
		src.m_dwBufSize = 0;
	}

private:
	void Initialize();
};
