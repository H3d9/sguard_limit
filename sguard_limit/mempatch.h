#pragma once
#include <Windows.h>
#include <vector>
#include <memory> // std::unique_ptr


// mempatch module (sington)
class PatchManager {

private:
	static PatchManager        patchManager;

private:
	PatchManager();
	~PatchManager()                                  = default;
	PatchManager(const PatchManager&)                = delete;
	PatchManager(PatchManager&&)                     = delete;
	PatchManager& operator= (const PatchManager&)    = delete;
	PatchManager& operator= (PatchManager&&)         = delete;

public:
	static PatchManager&       getInstance();

public:
	struct patchSwitches_t {
		bool NtQueryVirtualMemory    = false;
		bool GetAsyncKeyState        = false;
		bool NtWaitForSingleObject   = false;
		bool NtDelayExecution        = false;
		bool DeviceIoControl_1       = false;
		bool DeviceIoControl_2       = false;
	};
	struct patchDelayRange_t {
		DWORD low, def, high;
	};
	struct patchStatus_t {
		bool NtQueryVirtualMemory    = false;
		bool GetAsyncKeyState        = false;
		bool NtWaitForSingleObject   = false;
		bool NtDelayExecution        = false;
		bool DeviceIoControl_1       = false;
		bool DeviceIoControl_2       = false;
	};

	volatile bool                 patchEnabled;

	volatile patchSwitches_t      patchSwitches;
	volatile DWORD                patchDelay[4];        // ioctl dont need delayexecution
	const patchDelayRange_t       patchDelayRange[4];

	volatile DWORD                patchPid;
	volatile patchStatus_t        patchStatus;
	volatile int                  patchFailCount;

	volatile bool                 useAdvancedSearch;
	volatile DWORD                patchDelayBeforeNtdllioctl;
	volatile DWORD                patchDelayBeforeNtdlletc;

public:
	void      patch();
	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);

private:
	bool                      _patch_ntdll(DWORD pid, patchSwitches_t switches);
	bool                      _patch_user32(DWORD pid, patchSwitches_t switches);
	std::vector<ULONG64>      _findRip(bool useAll = false);
	void                      _outVmbuf(ULONG64, const CHAR*);

private:
	ULONG64                   vmStartAddress;
	std::unique_ptr<CHAR[]>   vmbuf_ptr;
	std::unique_ptr<CHAR[]>   vmalloc_ptr;
};