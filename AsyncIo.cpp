#include "StdAfx.h"
#include "AsyncIo.h"

AsyncIo::AsyncIo(void)
{
	Initialize();
}

AsyncIo::AsyncIo(DWORD dwBufSize) {
	Initialize();
	AllocateBuffer(dwBufSize);
}

AsyncIo::AsyncIo(unsigned __int64 offset, DWORD dwBufSize)
{
	Initialize();
	AllocateBuffer(dwBufSize);

	ULARGE_INTEGER ulOffset;
	ulOffset.QuadPart = offset;
	m_ol.OffsetHigh = ulOffset.HighPart;
	m_ol.Offset = ulOffset.LowPart;
}

AsyncIo::~AsyncIo(void)
{
	if (m_buffer != NULL) {
		delete[] m_buffer;
		m_buffer = NULL;
	}
}

bool AsyncIo::AllocateBuffer(DWORD dwBufSize) {
	if (m_buffer != NULL) {
		delete[] m_buffer;
	}

	m_buffer = new BYTE[dwBufSize];	
	m_dwBufSize = dwBufSize;
	::memset(m_buffer, 0, m_dwBufSize);

	return (m_buffer != NULL);
}

void AsyncIo::OnIoComplete(DWORD dwBytesTransferred, ULONG_PTR lpCompletionKey) {
	m_dwBytesXfered = dwBytesTransferred;
}

void AsyncIo::Initialize() {
	::memset(&m_ol, 0, sizeof(m_ol));
	::memset(&m_tpe, 0, sizeof(m_tpe));
	m_ioHandle = NULL;
	m_dwBytesXfered = 0;
	m_dwBufLength = 0;
	m_dwBufSize = 0;
	m_buffer = NULL;
	m_dwCookie = 0;

}