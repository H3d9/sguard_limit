#pragma once
#include <Windows.h>


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
	void     init(const CHAR* sysfilepath);
	bool     load();
	void     unload();
	bool     readVM(DWORD pid, PVOID out, PVOID targetAddress);
	bool     writeVM(DWORD pid, PVOID in, PVOID targetAddress);
	bool     allocVM(DWORD pid, PVOID* pAllocatedAddress);


private:
	bool     _startService();
	void     _endService();

private:
	typedef struct {
		CHAR       data[4096];
		PVOID      address;
		HANDLE     pid;

		CHAR       errorFunc[128];
		ULONG      errorCode;
	} VMIO_REQUEST;

	static constexpr DWORD   VMIO_READ   = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_WRITE  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	static constexpr DWORD   VMIO_ALLOC  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	const CHAR*    sysfile;
	SC_HANDLE      hSCManager;
	SC_HANDLE      hService;
	HANDLE         hDriver;

public:
	CHAR     errorMessage[1024];
	DWORD    errorCode;

private:
	void     _recordError(const CHAR* msg, ...);
};