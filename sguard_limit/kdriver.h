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


private:
	bool     _startService();
	void     _endService();

private:
	struct VMIO_REQUEST {
		HANDLE   pid;

		PVOID    address;
		CHAR     data[0x1000];

		ULONG    errorCode;
		CHAR     errorFunc[128];
	};

	static constexpr DWORD   VMIO_READ   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_WRITE  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_ALLOC  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_SUSPEND  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0704, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD	 IO_RESUME   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0705, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	std::string    sysfile;
	SC_HANDLE      hSCManager;
	SC_HANDLE      hService;
	HANDLE         hDriver;

public:
	DWORD          errorCode;
	CHAR*          errorMessage;

private:
	std::unique_ptr<CHAR[]>  errorMessage_ptr;
	void                     _recordError(const CHAR* msg, ...);
};