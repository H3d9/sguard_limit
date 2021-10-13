#pragma once
#include <Windows.h>

// wndproc button command
#define IDM_DOPATCH         214
#define IDM_UNDOPATCH       215
#define IDM_SETDELAY        216
#define IDM_PATCHSWITCH1    217
#define IDM_PATCHSWITCH2    218
#define IDM_PATCHSWITCH3    219


// driver io module
class KernelDriver {

public:
	KernelDriver();
	~KernelDriver();
	KernelDriver(const KernelDriver&)                = delete;
	KernelDriver(KernelDriver&&)                     = delete;
	KernelDriver& operator= (const KernelDriver&)    = delete;
	KernelDriver& operator= (KernelDriver&&)         = delete;

public:
	bool     load();
	void     unload();
	bool     readVM(HANDLE pid, PVOID out, PVOID targetAddress);
	bool     writeVM(HANDLE pid, PVOID in, PVOID targetAddress);

private:
	typedef struct {
		CHAR       data[4096];
		PVOID      address;
		HANDLE     pid;
				   
		CHAR       errorFunc[128];
		ULONG      errorCode;
	} VMIO_REQUEST;

	static constexpr DWORD   VMIO_READ    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_WRITE   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	HANDLE  hDriver;
};


// patch module
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
		bool patchDelayExecution        = false;
		bool patchResumeThread          = true;
		bool patchQueryVirtualMemory    = true;
	};

	volatile bool              patchEnabled;
	volatile DWORD             patchPid;
	volatile patchSwitches_t   patchSwitches;
	volatile DWORD             patchDelay;

public:
	void      patchInit();
	void      patch();
	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);
	void      wndProcAddMenu(HMENU hMenu);

private:
	ULONG64   _findRip();

private:
	ULONG64                    vmStartAddress             = 0;
	CHAR                       original_vm     [0x4000]   = {};
	CHAR                       commited_vm     [0x4000]   = {};
	CHAR                       vmbuf           [0x4000]   = {};
};