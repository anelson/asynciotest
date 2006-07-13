#pragma once
#include "asyncio.h"

struct FileAsyncIo :
	public AsyncIo
{
public:
	FileAsyncIo(DWORD dwBufSize);
	FileAsyncIo(DWORD dwOffset, DWORD dwBufSize);
	~FileAsyncIo(void);
};
