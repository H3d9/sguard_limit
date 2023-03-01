#pragma once
#include <winnt.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <tuple>
#include <tl/expected.hpp> // c++23 p0323r3: not implemented in msvc 14.2

using error_t  = std::tuple<std::string, DWORD>;
using result_t = tl::expected<bool, error_t>;


// kernel driver module (sington)
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
	result_t   init(std::string loadPath);
	
	result_t   load();
	void       unload();
	result_t   readVM(DWORD pid, PVOID out, PVOID targetAddress);
	result_t   writeVM(DWORD pid, PVOID in, PVOID targetAddress);
	result_t   allocVM(DWORD pid, PVOID* pAllocatedAddress);
	result_t   suspend(DWORD pid);
	result_t   resume(DWORD pid);
	result_t   searchVad(DWORD pid, std::vector<ULONG64>& out, const wchar_t* moduleName);
	result_t   restoreVad(/* param in kernel */);
	result_t   patchAceBase();


public:
	// [out] whether kdriver is ready to use.
	// flag returned from init(); decide accessibility to some menu options.
	bool       driverReady;

	// [xref] assert use same kernel offset, despite of the risk of bsod.
	// flag read from config; decide if win11 latest check is ignored.
	bool       win11ForceEnable;

	// [xref] current win11 build number.
	// num read from config; decide if win11 has updated.
	DWORD      win11CurrentBuild;


private:
	result_t     _checkVersion();
	result_t     _extractResource();
	std::string  _strUserManual();

private:
	struct service_guard {
		SC_HANDLE hSCManager = NULL;
		SC_HANDLE hService = NULL;

		~service_guard() {
			if (hService) {
				CloseServiceHandle(hService);
			}
			if (hSCManager) {
				CloseServiceHandle(hSCManager);
			}
		}
	};

	result_t   _startService();
	void       _endService();

private:
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
	std::string         sysfile_LoadPath;
	std::string         sysfile_CurPath;
	HANDLE              hDriver;

	std::atomic<DWORD>  loadCount;  // thread sync for: load/unload
	std::mutex          loadLock;
};