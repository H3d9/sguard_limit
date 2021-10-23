#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "kdriver.h"


// driver io
KernelDriver  KernelDriver::kernelDriver;

KernelDriver::KernelDriver()
	: hDriver(INVALID_HANDLE_VALUE), sysfile(NULL), errorMessage{}, errorCode(0) {}

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

		// this part of api is not easy to use. use cmd instead.
		// shellexec is async in case here it shall wait. total cost: 4`5s
		ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
		Sleep(1000);
		ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
		Sleep(1000);
		char arg[1024] = "create SGuardLimit_VMIO type= kernel binPath= \"";
		strcat(arg, sysfile);
		strcat(arg, "\"");
		ShellExecute(0, "open", "sc", arg, 0, SW_HIDE);
		Sleep(1000);
		ShellExecute(0, "open", "sc", "start SGuardLimit_VMIO", 0, SW_HIDE);
		Sleep(1000);

		hDriver = CreateFile("\\\\.\\SGuardLimit_VMIO", GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		
		if (hDriver == INVALID_HANDLE_VALUE) {
			_recordError("CreateFile ß∞‹°£");
			return false;
		}
	}

	return true;
}

void KernelDriver::unload() {

	if (hDriver != INVALID_HANDLE_VALUE) {

		CloseHandle(hDriver);
		hDriver = INVALID_HANDLE_VALUE;

		ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
		Sleep(1000);
		ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
		Sleep(1000);
	}
}

bool KernelDriver::readVM(DWORD pid, PVOID out, PVOID targetAddress) {

	// assert: "out" is a 16K buffer.
	VMIO_REQUEST  ReadRequest;
	DWORD         Bytes;

	ReadRequest.pid = (HANDLE)pid;
	ReadRequest.errorCode = 0;


	for (auto page = 0; page < 4; page++) {

		ReadRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_READ, &ReadRequest, sizeof(ReadRequest), &ReadRequest, sizeof(ReadRequest), &Bytes, NULL)) {
			_recordError("driver::readVM(): DeviceIoControl ß∞‹°£");
			return false;
		}
		if (ReadRequest.errorCode != 0) {
			_recordError("driver::readVM(): «˝∂Øƒ⁄≤ø¥ÌŒÛ£∫%s(0x%x)°£", ReadRequest.errorFunc, ReadRequest.errorCode);
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

	WriteRequest.pid = (HANDLE)pid;
	WriteRequest.errorCode = 0;


	for (auto page = 0; page < 4; page++) {

		WriteRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		memcpy(WriteRequest.data, (PVOID)((ULONG64)in + page * 0x1000), 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_WRITE, &WriteRequest, sizeof(WriteRequest), &WriteRequest, sizeof(WriteRequest), &Bytes, NULL)) {
			_recordError("driver::writeVM(): DeviceIoControl ß∞‹°£");
			return false;
		}
		if (WriteRequest.errorCode != 0) {
			_recordError("driver::writeVM(): «˝∂Øƒ⁄≤ø¥ÌŒÛ£∫%s(0x%x)°£", WriteRequest.errorFunc, WriteRequest.errorCode);
			return false;
		}
	}

	return true;
}

bool KernelDriver::allocVM(DWORD pid, PVOID* pAllocatedAddress) {

	VMIO_REQUEST  AllocRequest;
	DWORD         Bytes;

	AllocRequest.pid = (HANDLE)pid;
	AllocRequest.address = NULL;
	AllocRequest.errorCode = 0;


	if (!DeviceIoControl(hDriver, VMIO_ALLOC, &AllocRequest, sizeof(AllocRequest), &AllocRequest, sizeof(AllocRequest), &Bytes, NULL)) {
		_recordError("driver::allocVM(): DeviceIoControl ß∞‹°£");
		return false;
	}
	if (AllocRequest.errorCode != 0) {
		_recordError("driver::allocVM(): «˝∂Øƒ⁄≤ø¥ÌŒÛ£∫%s(0x%x)°£", AllocRequest.errorFunc, AllocRequest.errorCode);
		return false;
	}

	*pAllocatedAddress = AllocRequest.address;

	return true;
}

void KernelDriver::_recordError(const CHAR* msg, ...) {

	va_list arg;
	va_start(arg, msg);
	vsprintf(errorMessage, msg, arg);
	va_end(arg);

	errorCode = GetLastError();
}