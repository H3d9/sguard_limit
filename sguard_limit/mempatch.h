#pragma once
#include <Windows.h>
#include <memory> // std::unique_ptr


// patch module (sington)
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
	struct patchStatus_t {
		bool stage1                  = false;
		bool stage2                  = false;
	};
	struct patchSwitches_t {
		bool NtQueryVirtualMemory    = true;
		bool GetAsyncKeyState        = true;
		bool NtWaitForSingleObject   = false;
		bool NtDelayExecution        = false;
	};
	struct patchDelayRange_t {
		DWORD low, def, high;
	};

	volatile bool                 patchEnabled;

	volatile DWORD                patchPid;
	volatile patchStatus_t        patchStatus;

	volatile patchSwitches_t      patchSwitches;
	volatile DWORD                patchDelay[4];
	const patchDelayRange_t       patchDelayRange[4];

public:
	void      init();
	void      patch();
	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);

private:
	bool                      _patch_stage1();
	bool                      _patch_stage2();
	std::vector<ULONG64>      _findRip(bool useAll = false);
	void                      _outVmbuf(ULONG64, const CHAR*);

private:
	ULONG64                   vmStartAddress;
	std::unique_ptr<CHAR[]>   vmbuf_ptr;
	std::unique_ptr<CHAR[]>   vmalloc_ptr;
};