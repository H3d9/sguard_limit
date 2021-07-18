#pragma once

struct lockedThreads_t {
	DWORD  tid    = 0;
	HANDLE handle = NULL;   // handle == NULL : not locked.
	bool   locked = false;
};

void threadLock(DWORD);