#pragma once
#include <Windows.h>


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
	struct patchSwitches_t {
		bool NtQueryVirtualMemory    = true;
		bool GetAsyncKeyState        = true;
		bool NtWaitForSingleObject   = false;
		bool NtDelayExecution        = false;
	};

	volatile bool                 patchEnabled;
	volatile DWORD                patchPid;
	volatile patchSwitches_t      patchSwitches;
	volatile DWORD                patchDelay[4];
	const struct 
	{ DWORD low, def, high; }     patchDelayRange[4];

	volatile bool                 patchv3ok;

public:
	void      init();
	void      patch();
	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);

private:
	std::vector<ULONG64>    _findRip();
	bool                    _patch_stage2();
	void                    _outVmbuf();
							
private:					
	ULONG64                 vmStartAddress             = 0;
	CHAR                    vmbuf           [0x4000]   = {};
	CHAR                    vmalloc         [0x4000]   = {};
};