#pragma once
#include <Windows.h>
#include <string>
#include <memory>


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
	bool     init(const std::string& sysfileDir);
	bool     load();
	void     unload();
	bool     readVM(DWORD pid, PVOID out, PVOID targetAddress);
	bool     writeVM(DWORD pid, PVOID in, PVOID targetAddress);
	bool     allocVM(DWORD pid, PVOID* pAllocatedAddress);
	bool     suspend(DWORD pid);
	bool     resume(DWORD pid);

public:
	bool     driverReady;  // driver ready if init() success. load() & unload() is still required.
	                       // this switch is used for decide usability of some menu options.


private:
	bool     _startService();
	void     _endService();

private:
	struct VMIO_REQUEST {
		HANDLE   pid;

		PVOID    address           = NULL;
		CHAR     data   [0x1000]   = {};

		ULONG    errorCode         = 0;
		CHAR     errorFunc [128]   = {};

		VMIO_REQUEST(DWORD pid) : pid(reinterpret_cast<HANDLE>(static_cast<LONG64>(pid))) {}
	};

	static constexpr DWORD   VMIO_READ   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_WRITE  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_ALLOC  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_SUSPEND  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0704, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_RESUME   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0705, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	std::string     sysfile;
	SC_HANDLE       hSCManager;
	SC_HANDLE       hService;
	HANDLE          hDriver;


public:
	volatile DWORD  errorCode;     // module's errors recorded here (if method returns false).
	const CHAR*     errorMessage;  // caller can decide to log, panic, or ignore.

private:
	void            _resetError();
	void            _recordError(DWORD errorCode, const CHAR* msg, ...);

private:
	std::unique_ptr<CHAR[]>  errorMessage_ptr;
};