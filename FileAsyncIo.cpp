#include "StdAfx.h"
#include ".\fileasyncio.h"

FileAsyncIo::FileAsyncIo(DWORD dwBufSize) : AsyncIo(dwBufSize)
{
}

FileAsyncIo::FileAsyncIo(DWORD dwOffset, DWORD dwBufSize) : AsyncIo(dwBufSize)
{
	m_dwOffset = dwOffset;
	m_dwBytesIn = dwBufSize;

	m_ol.OffsetHigh = 0;
	m_ol.Offset = dwOffset;
}

FileAsyncIo::~FileAsyncIo(void)
{
}
