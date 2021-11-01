#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "kdriver.h"


// driver io
KernelDriver  KernelDriver::kernelDriver;

KernelDriver::KernelDriver()
	: sysfile(NULL), hSCManager(NULL), hService(NULL), hDriver(INVALID_HANDLE_VALUE), errorMessage{}, errorCode(0) {}

KernelDriver::~KernelDriver() {
	unload();
}

KernelDriver& KernelDriver::getInstance() {
	return kernelDriver;
}

void KernelDriver::init(const CHAR* sysfilepath) {
	this->sysfile = sysfilepath;
}

bool KernelDriver::load() {

	if (hDriver == INVALID_HANDLE_VALUE) {

		if (!_startService()) {
			return false;
		}

		hDriver = CreateFile("\\\\.\\SGuardLimit_VMIO", GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		
		if (hDriver == INVALID_HANDLE_VALUE) {
			_recordError("CreateFile失败。");
			return false;
		}
	}

	return true;
}

void KernelDriver::unload() {

	if (hDriver != INVALID_HANDLE_VALUE) {

		CloseHandle(hDriver);
		hDriver = INVALID_HANDLE_VALUE;

		_endService();
	}
}

bool KernelDriver::readVM(DWORD pid, PVOID out, PVOID targetAddress) {

	// assert: "out" is a 16K buffer.
	VMIO_REQUEST  ReadRequest;
	DWORD         Bytes;

	ReadRequest.pid = reinterpret_cast<HANDLE>(static_cast<LONG64>(pid));
	ReadRequest.errorCode = 0;


	for (auto page = 0; page < 4; page++) {

		ReadRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_READ, &ReadRequest, sizeof(ReadRequest), &ReadRequest, sizeof(ReadRequest), &Bytes, NULL)) {
			_recordError("driver::readVM(): DeviceIoControl失败。");
			return false;
		}
		if (ReadRequest.errorCode != 0) {
			_recordError("driver::readVM(): 驱动内部错误：%s。\n【注】SGuard进程可能已经关闭。建议你重启游戏。", ReadRequest.errorFunc);
			this->errorCode = ReadRequest.errorCode;
			return false;
		}

		memcpy((PVOID)((ULONG64)out + page * 0x1000), ReadRequest.data, 0x1000);
	}

	return true;
}

bool KernelDriver::writeVM(DWORD pid, PVOID in, PVOID targetAddress) {

	// assert: "in" is a 16K buffer.
	VMIO_REQUEST  WriteRequest;
	DWORD         Bytes;

	WriteRequest.pid = reinterpret_cast<HANDLE>(static_cast<LONG64>(pid));
	WriteRequest.errorCode = 0;


	for (auto page = 0; page < 4; page++) {

		WriteRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		memcpy(WriteRequest.data, (PVOID)((ULONG64)in + page * 0x1000), 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_WRITE, &WriteRequest, sizeof(WriteRequest), &WriteRequest, sizeof(WriteRequest), &Bytes, NULL)) {
			_recordError("driver::writeVM(): DeviceIoControl失败。");
			return false;
		}
		if (WriteRequest.errorCode != 0) {
			_recordError("driver::writeVM(): 驱动内部错误：%s。\n【注】SGuard进程可能已经关闭。建议你重启游戏。", WriteRequest.errorFunc);
			this->errorCode = WriteRequest.errorCode;
			return false;
		}
	}

	return true;
}

bool KernelDriver::allocVM(DWORD pid, PVOID* pAllocatedAddress) {

	VMIO_REQUEST  AllocRequest;
	DWORD         Bytes;

	AllocRequest.pid = reinterpret_cast<HANDLE>(static_cast<LONG64>(pid));
	AllocRequest.address = NULL;
	AllocRequest.errorCode = 0;


	if (!DeviceIoControl(hDriver, VMIO_ALLOC, &AllocRequest, sizeof(AllocRequest), &AllocRequest, sizeof(AllocRequest), &Bytes, NULL)) {
		_recordError("driver::allocVM(): DeviceIoControl失败。");
		return false;
	}
	if (AllocRequest.errorCode != 0) {
		_recordError("driver::allocVM(): 驱动内部错误：%s。\n【注】SGuard进程可能已经关闭。建议你重启游戏。", AllocRequest.errorFunc);
		this->errorCode = AllocRequest.errorCode;
		return false;
	}

	*pAllocatedAddress = AllocRequest.address;

	return true;
}

#define SVC_ERROR_EXIT(errorMsg)   _recordError(errorMsg); \
                                   if (hService) CloseServiceHandle(hService); \
                                   if (hSCManager) CloseServiceHandle(hSCManager); \
                                   hService = NULL; \
                                   hSCManager = NULL; \
                                   return false;

bool KernelDriver::_startService() {

	SERVICE_STATUS svcStatus;

	// open SCM.
	hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (!hSCManager) {
		SVC_ERROR_EXIT("OpenSCManager失败。");
	}

	// open Service.
	hService = OpenService(hSCManager, "SGuardLimit_VMIO", SERVICE_ALL_ACCESS);

	if (!hService) {
		hService = 
		CreateService(hSCManager, "SGuardLimit_VMIO", "SGuardLimit_VMIO",
			SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
			sysfile,  /* no quote here, msdn e.g. is wrong */ /* assert path is valid */
			NULL, NULL, NULL, NULL, NULL);

		if (!hService) {
			SVC_ERROR_EXIT("CreateService失败。");
		}
	}

	// check service status.
	QueryServiceStatus(hService, &svcStatus);

	// if service is running, stop it.
	if (svcStatus.dwCurrentState != SERVICE_STOPPED && svcStatus.dwCurrentState != SERVICE_STOP_PENDING) {
		if (!ControlService(hService, SERVICE_CONTROL_STOP, &svcStatus)) {
			DeleteService(hService);
			SVC_ERROR_EXIT("无法停止当前服务。重启电脑应该能解决该问题。");
		}
	}

	// if service is stopping,
	// wait till it's completely stopped. 
	if (svcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
		for (auto time = 0; time < 50; time++) {
			Sleep(100);
			QueryServiceStatus(hService, &svcStatus);
			if (svcStatus.dwCurrentState == SERVICE_STOPPED) {
				break;
			}
		}

		if (svcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
			DeleteService(hService);
			SVC_ERROR_EXIT("停止当前服务时的等待时间过长。重启电脑应该能解决该问题。");
		}
	}

	// start service.
	// if start failed, delete old one (cause old service may have wrong params)
	if (!StartService(hService, 0, NULL)) {
		DeleteService(hService);
		SVC_ERROR_EXIT("StartService失败。重启电脑应该能解决该问题。");
	}

	return true;
}

void KernelDriver::_endService() {

	SERVICE_STATUS svcStatus;

	// stop service.
	ControlService(hService, SERVICE_CONTROL_STOP, &svcStatus);

	// mark service to be deleted.
	DeleteService(hService);

	// service will be deleted after we close service handle.
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	hService = NULL;
	hSCManager = NULL;
}

void KernelDriver::_recordError(const CHAR* msg, ...) {

	va_list arg;
	va_start(arg, msg);
	vsprintf(errorMessage, msg, arg);
	va_end(arg);

	errorCode = GetLastError();
}