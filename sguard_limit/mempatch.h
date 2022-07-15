#pragma once
#include <Windows.h>
#include <vector>
#include <unordered_map>
#include <memory>  // std::unique_ptr
#include <atomic>


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
	typedef struct tagPatchSwitches_t {
		std::atomic<bool>   NtQueryVirtualMemory     = false;
		std::atomic<bool>   NtReadVirtualMemory      = false; // no delay
		std::atomic<bool>   GetAsyncKeyState         = false;
		std::atomic<bool>   NtWaitForSingleObject    = false;
		std::atomic<bool>   NtDelayExecution         = false;
		std::atomic<bool>   DeviceIoControl_1        = false; // no delay
		std::atomic<bool>   DeviceIoControl_2        = false; // no delay
	} patchSwitches_t, patchStatus_t;

	struct patchDelayRange_t {
		DWORD low, def, high;
	};

	std::atomic<bool>             patchEnabled;

	patchSwitches_t               patchSwitches;
	std::atomic<DWORD>            patchDelay[4];
	const patchDelayRange_t       patchDelayRange[4];

	std::atomic<DWORD>            patchPid;
	patchStatus_t                 patchStatus;
	std::atomic<int>              patchFailCount;

	std::atomic<bool>             useAdvancedSearch;
	std::atomic<DWORD>            patchDelayBeforeNtdllioctl;
	std::atomic<DWORD>            patchDelayBeforeNtdlletc;

public:
	bool      init();
	void      patch();
	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);


private:
	DWORD                     _getSyscallNumber(const char* funcName, const char* libName);
	bool                      _patch_ntdll(DWORD pid, patchSwitches_t& switches);
	bool                      _patch_user32(DWORD pid, patchSwitches_t& switches);
	std::vector<ULONG64>      _findRip(bool useAll = false);
	void                      _outVmbuf(ULONG64, const char*);

private:
	std::unordered_map<std::string, DWORD>   syscallTable;  // func name -> native syscall num
	ULONG64                                  vmStartAddress;
	std::unique_ptr<char[]>                  vmbuf_ptr;
	std::unique_ptr<char[]>                  vmalloc_ptr;
};