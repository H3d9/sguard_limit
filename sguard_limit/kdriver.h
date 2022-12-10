#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>


// driver io module (sington)
class KernelDriver {

private:
	static KernelDriver  kernelDriver;

private:
	KernelDriver();
	~KernelDriver();
	KernelDriver(const KernelDriver&)               = delete;
	KernelDriver(KernelDriver&&)                    = delete;
	KernelDriver& operator= (const KernelDriver&)   = delete;
	KernelDriver& operator= (KernelDriver&&)        = delete;

public:
	static KernelDriver& getInstance();
	

public:
	bool     init(std::string loadPath);
	
	bool     load();
	void     unload();
	bool     readVM(DWORD pid, PVOID out, PVOID targetAddress);
	bool     writeVM(DWORD pid, PVOID in, PVOID targetAddress);
	bool     allocVM(DWORD pid, PVOID* pAllocatedAddress);
	bool     suspend(DWORD pid);
	bool     resume(DWORD pid);
	bool     searchVad(DWORD pid, std::vector<ULONG64>& out, const wchar_t* moduleName);
	bool     restoreVad(/* param in kernel */);
	bool     patchAceBase();

public:
	bool     driverReady;         // [out] whether kdriver is ready to use.
	                              // flag returned from init(); decide accessibility to some menu options.
	bool     win11ForceEnable;    // [xref] assert use same kernel offset, despite of the risk of bsod.
								  // flag read from config; decide if win11 latest check is ignored.
	DWORD    win11CurrentBuild;   // [xref] current win11 build number.
								  // num read from config; decide if win11 has updated.

private:
	bool     _startService();
	void     _endService();

	struct service_guard {
		SC_HANDLE hSCManager = NULL;
		SC_HANDLE hService   = NULL;

		~service_guard() {
			if (hService) {
				CloseServiceHandle(hService);
			}
			if (hSCManager) {
				CloseServiceHandle(hSCManager);
			}
		}
	};

	
private:
	bool     _checkSysVersion();

	struct VMIO_REQUEST {
		HANDLE   pid;

		PVOID    address           = NULL;
		CHAR     data   [0x1000]   = {};

		ULONG    errorCode         = 0;
		CHAR     errorFunc [128]   = {};

		VMIO_REQUEST(DWORD pid) : pid(reinterpret_cast<HANDLE>(static_cast<LONG64>(pid))) {}
	};

	static constexpr DWORD   VMIO_VERSION   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0700, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_READ      = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_WRITE     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_ALLOC     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_SUSPEND     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0704, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_RESUME      = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0705, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 VM_VADSEARCH   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0706, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 VM_VADRESTORE  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0707, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 PATCH_ACEBASE  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0708, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);


private:
	std::string         sysfile;
	HANDLE              hDriver;

	std::atomic<DWORD>  loadCount;  // multi-thread call sync
	std::mutex          loadLock;
	

public:
	std::atomic<DWORD>  errorCode;     // module's errors recorded here (if method returns false).
	const char*         errorMessage;  // caller can decide to log, panic, or ignore.

private:
	void            _resetError();  // thread unsafe
	void            _recordError(DWORD errorCode, const char* msg, ...);

	std::unique_ptr<char[]>  errorMessage_ptr;
};