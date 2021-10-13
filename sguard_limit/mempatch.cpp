// Memory Patch（用户态模块）
// 2021.10.4 雨
// 昨天吃坏肚子了，很疼。但是 2.2 复刻胡桃，开心。
#include <Windows.h>
#include <tlhelp32.h>
#include <UserEnv.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <map>
#include "wndproc.h"
#include "win32utility.h"
#include "panic.h"

#include "mempatch.h"

extern volatile bool        g_bHijackThreadWaiting;
extern win32SystemManager&  systemMgr;


// driver io
KernelDriver::KernelDriver() 
	: hDriver(INVALID_HANDLE_VALUE) {}

KernelDriver::~KernelDriver() {
	if (hDriver != INVALID_HANDLE_VALUE) {
		unload();
	}
}

bool KernelDriver::load() {

	// this part of api is complex. use cmd instead.
	// shellexec is async in case here it shall wait. total cost: 4`5s
	ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(1000);
	ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(1000);
	char arg[1024] = "create SGuardLimit_VMIO type= kernel binPath= ";
	strcat(arg, systemMgr.sysfilePath());
	ShellExecute(0, "open", "sc", arg, 0, SW_HIDE);
	Sleep(1000);
	ShellExecute(0, "open", "sc", "start SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(1000);

	hDriver = CreateFile("\\\\.\\SGuardLimit_VMIO", GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);

	return hDriver != INVALID_HANDLE_VALUE;
}

void KernelDriver::unload() {

	CloseHandle(hDriver);

	ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(1000);
	ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(1000);

	hDriver = INVALID_HANDLE_VALUE;
}

bool KernelDriver::readVM(HANDLE pid, PVOID out, PVOID targetAddress) {

	// assert: "out" is a 16K buffer.
	VMIO_REQUEST  ReadRequest;
	DWORD         Bytes;

	ReadRequest.pid = pid;
	ReadRequest.errorCode = 0;


	for (auto page = 0; page < 4; page++) {

		ReadRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_READ, &ReadRequest, sizeof(ReadRequest), &ReadRequest, sizeof(ReadRequest), &Bytes, NULL)) {
			return false;
		}
		if (ReadRequest.errorCode != 0) {
			panic("驱动内部错误：%s(%x)", ReadRequest.errorFunc, ReadRequest.errorCode);
			return false;
		}

		memcpy((PVOID)((ULONG64)out + page * 0x1000), ReadRequest.data, 0x1000);
	}

	return true;
}

bool KernelDriver::writeVM(HANDLE pid, PVOID in, PVOID targetAddress) {

	// assert: "in" is a 16K buffer.
	VMIO_REQUEST  WriteRequest;
	DWORD         Bytes;

	WriteRequest.pid = pid;
	WriteRequest.errorCode = 0;

	for (auto page = 0; page < 4; page++) {

		WriteRequest.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		memcpy(WriteRequest.data, (PVOID)((ULONG64)in + page * 0x1000), 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_WRITE, &WriteRequest, sizeof(WriteRequest), &WriteRequest, sizeof(WriteRequest), &Bytes, NULL)) {
			return false;
		}
		if (WriteRequest.errorCode != 0) {
			panic("驱动内部错误：%s(0x%x)", WriteRequest.errorFunc, WriteRequest.errorCode);
			return false;
		}
	}

	return true;
}


// patch module
PatchManager  PatchManager::patchManager;

PatchManager::PatchManager()
	: patchEnabled(true), patchPid(0), patchSwitches(), patchDelay(1250) { /* private members use default init */ }

PatchManager& PatchManager::getInstance() {
	return patchManager;
}

void PatchManager::patchInit() {

	// import certificate key.
	HKEY       key;
	DWORD      dwDisposition;
	const BYTE keyData[] = "\x53\x00\x00\x00\x01\x00\x00\x00\x23\x00\x00\x00\x30\x21\x30\x1f\x06\x09\x60\x86\x48\x01"
		"\xa4\xa2\x27\x02\x01\x30\x12\x30\x10\x06\x0a\x2b\x06\x01\x04\x01\x82\x37\x3c\x01\x01\x03\x02\x00\xc0"
		"\x5c\x00\x00\x00\x01\x00\x00\x00\x04\x00\x00\x00\x00\x10\x00\x00\x03\x00\x00\x00\x01\x00\x00\x00\x14"
		"\x00\x00\x00\xe4\x03\xa1\xdf\xc8\xf3\x77\xe0\xf4\xaa\x43\xa8\x3e\xe9\xea\x07\x9a\x1f\x55\xf2\x19\x00"
		"\x00\x00\x01\x00\x00\x00\x10\x00\x00\x00\x79\xd8\xe3\x98\x56\xb0\x54\x09\x13\xde\xfb\x48\x5e\x73\xed"
		"\x62\x14\x00\x00\x00\x01\x00\x00\x00\x14\x00\x00\x00\x05\x25\x86\x2f\x65\x36\xa1\xe5\x9d\x9e\xca\x5c"
		"\x09\x19\xad\x0e\x3d\x96\x26\x1d\x0f\x00\x00\x00\x01\x00\x00\x00\x14\x00\x00\x00\x52\xbf\x46\x22\x03"
		"\x12\x1a\xb2\x71\xf4\x8f\xf1\xa3\x2d\x37\x3f\xd9\xf1\x23\x99\x04\x00\x00\x00\x01\x00\x00\x00\x10\x00"
		"\x00\x00\xdc\x91\x1e\x8d\xa3\xa1\x86\xbb\x4d\x52\xee\xc0\xe5\x7b\x51\x55\x09\x00\x00\x00\x01\x00\x00"
		"\x00\x4c\x00\x00\x00\x30\x4a\x06\x0a\x2b\x06\x01\x04\x01\x82\x37\x0a\x06\x02\x06\x0a\x2b\x06\x01\x04"
		"\x01\x82\x37\x0a\x06\x01\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x08\x06\x08\x2b\x06\x01\x05\x05\x07\x03"
		"\x04\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x03\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x02\x06\x08\x2b\x06"
		"\x01\x05\x05\x07\x03\x01\x20\x00\x00\x00\x01\x00\x00\x00\xd3\x05\x00\x00\x30\x82\x05\xcf\x30\x82\x03"
		"\xb7\xa0\x03\x02\x01\x02\x02\x04\x1e\xb1\x32\xd5\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05"
		"\x05\x00\x30\x76\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x43\x4e\x31\x23\x30\x21\x06\x03\x55\x04"
		"\x0a\x0c\x1a\x4a\x65\x6d\x6d\x79\x4c\x6f\x76\x65\x4a\x65\x6e\x6e\x79\x20\x50\x4b\x49\x20\x53\x65\x72"
		"\x76\x69\x63\x65\x31\x1e\x30\x1c\x06\x03\x55\x04\x0b\x0c\x15\x70\x6b\x69\x2e\x6a\x65\x6d\x6d\x79\x6c"
		"\x6f\x76\x65\x6a\x65\x6e\x6e\x79\x2e\x74\x6b\x31\x22\x30\x20\x06\x03\x55\x04\x03\x0c\x19\x4a\x65\x6d"
		"\x6d\x79\x4c\x6f\x76\x65\x4a\x65\x6e\x6e\x79\x20\x45\x56\x20\x52\x6f\x6f\x74\x20\x43\x41\x30\x20\x17"
		"\x0d\x30\x30\x30\x31\x30\x31\x30\x30\x30\x30\x30\x30\x5a\x18\x0f\x32\x30\x39\x39\x31\x32\x33\x31\x32"
		"\x33\x35\x39\x35\x39\x5a\x30\x76\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x43\x4e\x31\x23\x30\x21"
		"\x06\x03\x55\x04\x0a\x0c\x1a\x4a\x65\x6d\x6d\x79\x4c\x6f\x76\x65\x4a\x65\x6e\x6e\x79\x20\x50\x4b\x49"
		"\x20\x53\x65\x72\x76\x69\x63\x65\x31\x1e\x30\x1c\x06\x03\x55\x04\x0b\x0c\x15\x70\x6b\x69\x2e\x6a\x65"
		"\x6d\x6d\x79\x6c\x6f\x76\x65\x6a\x65\x6e\x6e\x79\x2e\x74\x6b\x31\x22\x30\x20\x06\x03\x55\x04\x03\x0c"
		"\x19\x4a\x65\x6d\x6d\x79\x4c\x6f\x76\x65\x4a\x65\x6e\x6e\x79\x20\x45\x56\x20\x52\x6f\x6f\x74\x20\x43"
		"\x41\x30\x82\x02\x22\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00\x03\x82\x02\x0f\x00"
		"\x30\x82\x02\x0a\x02\x82\x02\x01\x00\xb5\xbf\x16\x4c\xe2\x67\x33\x2d\x80\xff\xed\x87\xe9\x49\x04\x1e"
		"\xa0\xb8\xdd\x6e\x43\x89\xcc\x2e\xce\x1e\x26\x06\xc7\xdc\x40\x85\xd7\x56\x31\xf5\xbf\x99\xe3\xb6\x0a"
		"\x4d\xbe\x48\xdc\x73\x37\xe8\xed\xc9\x5d\xd0\x2a\xca\x56\x8a\x11\x9c\x28\x84\xdd\x8c\xec\xd0\xc1\x74"
		"\x58\x5e\x1b\x6e\xc8\x9e\x47\xf3\x7f\x28\x62\x6b\xb4\x2a\xbb\x0f\x7c\xb0\xee\x0f\x25\xd1\x1e\x26\x80"
		"\x26\x93\x7b\xfc\x45\x87\xde\x5d\x7c\xd8\x9d\x9c\xd3\xfe\xe6\x34\x12\x07\x24\xa3\x77\x1d\x3d\xec\x3b"
		"\xa2\x65\x39\x8f\x84\x27\x4b\xc7\x2d\x68\x3b\xe7\x98\x27\x06\xd9\x9e\x24\xf4\xff\xe8\x43\x70\xfb\x7b"
		"\x0c\x8d\x56\x44\x9c\x1b\xdb\x2d\x53\xca\x85\xa5\x5e\xa1\x2b\x4c\xb6\x5a\xa6\x91\xfb\xbc\xeb\x57\xc3"
		"\xcb\x92\x4d\xed\x73\x2c\x25\x2a\x96\x80\x69\x03\x0d\xbd\x3a\x2b\xf0\xc8\xfa\x02\x7b\x7a\xb6\xaf\xc3"
		"\x25\xb4\x39\xd4\xed\xc7\xba\xd1\xd3\xe5\x7d\xfa\x24\x47\x05\xd3\x6b\xae\x51\x6d\xaa\x94\x37\x8e\xa3"
		"\xa8\x7e\x54\xaa\xd2\x1d\xeb\x54\x7b\x1b\xc6\x59\xca\x61\x1b\x05\xda\x6f\x47\x3f\x6d\xed\xfc\x76\x16"
		"\xbb\x9a\x86\x83\x8c\xb2\xb1\xbe\x86\xff\x21\x69\xd4\xbc\xc9\x07\x85\x27\xfa\x4e\x57\x9a\xcf\xc1\xd6"
		"\x49\x33\x97\x51\xc2\xe6\x52\x14\x32\xcf\x6b\x5f\x26\x66\x6f\x2c\x73\x2e\xc5\x67\xa2\xf5\xc8\x9f\x62"
		"\xa3\x4b\x4a\x73\x35\x22\x38\xb0\x2d\x98\x1e\xaf\x90\x5c\x6a\x66\xeb\xe5\x70\xb2\x0d\x6a\xe5\x7d\x97"
		"\x84\x20\xf3\x4a\xb6\x79\x26\x8d\x89\x10\x21\x70\x31\xfa\x6c\x69\x83\x1f\x48\x5e\xab\x30\xc4\x45\x78"
		"\x92\x42\x97\x7e\x2c\x9d\x2d\xf3\xf0\xf1\xaa\x4e\xc0\xca\xe5\x61\x24\x18\xff\xdf\x01\x27\xb7\xd5\x80"
		"\x9e\x7a\x18\x03\x12\x1d\x5b\x0f\xf8\x25\x37\xab\x11\x2a\x49\xd7\x94\x6a\x51\xec\x8c\x46\x91\x33\x2d"
		"\x5f\xfa\x41\x54\x71\xf2\xd9\x5e\x10\x44\x00\x77\x6c\x21\x25\x0a\xe0\x0d\x58\x7b\x23\x3b\x22\xa5\x96"
		"\xdb\x16\x9e\x05\x83\xc0\x02\x7c\x59\x81\x45\x44\x96\x3e\x66\xa5\xeb\x29\x3e\xa1\x15\x23\xe3\x38\xd9"
		"\x24\x24\x4b\xd3\x6b\x6d\x27\x22\x7e\xec\xf8\x48\xc3\xae\xf3\x9b\x75\x61\x23\x59\x5c\x64\x6d\x36\xd6"
		"\xcd\xf5\x70\xb7\x2f\xe9\xfb\xef\x77\x9e\x0a\xfa\x1d\xb7\xcf\x4c\xc8\x19\x64\xb3\x66\x44\x1f\x80\x32"
		"\x33\x7a\x32\x8f\x3c\x98\x89\x97\xd0\xa2\x7d\x2d\x8d\xce\x89\x1c\x22\x1a\x51\x4a\xb3\x02\x03\x01\x00"
		"\x01\xa3\x63\x30\x61\x30\x0e\x06\x03\x55\x1d\x0f\x01\x01\xff\x04\x04\x03\x02\x01\x86\x30\x0f\x06\x03"
		"\x55\x1d\x13\x01\x01\xff\x04\x05\x30\x03\x01\x01\xff\x30\x1d\x06\x03\x55\x1d\x0e\x04\x16\x04\x14\x05"
		"\x25\x86\x2f\x65\x36\xa1\xe5\x9d\x9e\xca\x5c\x09\x19\xad\x0e\x3d\x96\x26\x1d\x30\x1f\x06\x03\x55\x1d"
		"\x23\x04\x18\x30\x16\x80\x14\x05\x25\x86\x2f\x65\x36\xa1\xe5\x9d\x9e\xca\x5c\x09\x19\xad\x0e\x3d\x96"
		"\x26\x1d\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05\x05\x00\x03\x82\x02\x01\x00\xad\x21\xca"
		"\xaf\x24\xb3\xbf\xa5\xae\x38\x07\x83\x45\x3b\x61\x41\x9a\x46\x25\xb1\xad\xf9\x76\xca\x6e\xe7\x7f\x80"
		"\x20\x63\x84\x2f\xd2\xca\x48\x79\xdd\xf3\x9d\xf1\xa0\xca\x77\x9b\xb3\x13\xfb\x86\xd1\x24\x16\x07\xb6"
		"\xdf\x5e\x86\x8a\xd9\xcd\xdb\x69\xe1\x9b\xaf\x31\x07\xc2\x2c\xf9\x51\x56\x9d\xc8\xd5\xf8\x9d\xb4\xb4"
		"\xab\x7b\x85\x9b\x61\x48\x2b\x10\xdf\x9b\xfc\xce\x81\xc4\xf1\xb8\x6c\x77\xd4\x0a\x5e\xe2\x80\x5a\x46"
		"\x0d\xd0\xd6\xea\x16\x5f\x86\xe6\x70\x85\x09\x7d\x15\x90\x90\x41\x6b\x07\xde\x58\xec\xe9\x77\x64\xbd"
		"\x1a\xb9\xd3\xc1\x97\xd1\xe5\x2a\xa1\x32\x18\x2f\x68\xfe\x19\x62\xf1\x94\xb2\x2e\x1a\x5a\x9d\x4d\x25"
		"\xc4\x6c\x9e\x97\xa8\xa6\xfd\xe4\xec\x57\x29\x6b\x4a\x50\x9e\xb6\xdc\xc8\xbe\x7b\x25\xff\x10\x4e\xf9"
		"\x89\x2d\x41\x3c\x93\x66\x23\x51\xb7\xf3\xba\xb4\x72\x5a\xaa\xdd\x18\xad\xf6\x5e\xfb\xa7\x42\x24\xdb"
		"\xd1\xdd\x71\x83\x56\xd6\x8e\x20\x50\x46\xd6\x48\xac\x74\xe1\x1d\x3b\xe7\x49\x4d\x0d\xba\x37\xc5\x1a"
		"\x46\x7a\xf5\x7c\x72\x1a\x25\x96\x8a\xb1\xe6\xa4\x84\x16\x00\x9c\xfb\x2a\xc1\xc0\x63\x24\x5d\x93\xec"
		"\x24\x59\xac\x26\x27\x78\xe9\x07\xe4\xb9\xaa\x5b\xf6\x66\xdb\x80\x83\x44\xc8\x38\x57\xd6\x32\xae\x46"
		"\xfe\x2d\xab\xa8\x3d\x14\x97\xa1\x55\xbb\x9d\xa7\x67\xca\x7f\xe5\x67\xb2\x18\xdf\xfd\xa7\xaa\x61\x05"
		"\x55\x01\xb2\xf5\x15\xcd\x0f\xd8\xb8\x30\xa6\x7c\x82\xb7\x65\xf5\x2c\xf8\x0f\x07\x7e\xa1\x77\x48\x03"
		"\x95\xa2\x9c\x8d\xbe\x9c\xd5\x72\x71\x52\x57\xa7\xce\xf5\x8a\xd6\x32\x18\xe0\x5d\x16\xf0\x09\x6b\xd4"
		"\x96\xe1\x2d\x17\xbf\x77\x90\xb9\xdd\xb6\x31\x8f\xb9\x1a\x2a\x3f\x33\x95\xdd\x55\xe4\xba\x73\x9c\x2c"
		"\x8d\x15\x44\x2f\xbc\x8d\xe4\x1b\xad\xe1\x32\xd1\xa2\x3f\xb5\xa2\x84\x4e\x6b\x08\x06\x22\x08\xf9\x19"
		"\x1e\x1f\x3c\xa0\xa5\x50\x4e\x07\xcf\x4b\x3f\x2b\xaa\x68\x3b\xdc\x0d\xc1\x0a\x8a\x66\x31\xb3\xd4\x64"
		"\x81\x64\x17\x98\xa4\xa3\x7b\x35\xad\xa8\x00\x10\x4d\x8b\xc7\x0c\x0f\xc3\x1f\x66\x15\x28\x4c\xb1\x22"
		"\x95\x92\xee\x07\x21\x39\xb6\xa1\x5a\x8a\xd9\xe1\xa8\x13\x5b\xb4\xfe\x7b\x42\x6d\x5e\x69\xca\x1a\x9a"
		"\x42\x0d\x7c\xe3\x61\x24\x90\xd4\xd9\x42\x18\x35\xa8\x9a\x05\xf1\x4e\x3c\xa3\xfd\x98\x7c\x51\xcd\x62"
		"\xf2\x92\x69\x45\xcf\xbf\xf3\x2c\xaa";

	if (ERROR_SUCCESS != RegCreateKeyEx(
		HKEY_LOCAL_MACHINE,
		"SOFTWARE\\Microsoft\\SystemCertificates\\ROOT\\Certificates\\E403A1DFC8F377E0F4AA43A83EE9EA079A1F55F2\\",
		0, 0, 0, KEY_ALL_ACCESS, 0, &key, &dwDisposition)) {
		panic("RegCreateKeyEx失败，你可能需要手动安装证书");
	}

	if (dwDisposition == REG_CREATED_NEW_KEY) {
		if (ERROR_SUCCESS != RegSetValueEx(key, "Blob", 0, REG_BINARY, keyData, sizeof(keyData) - 1)) {
			panic("RegSetValueEx失败，你可能需要手动安装证书");
		}
		Sleep(1000);
	}

	RegCloseKey(key);


	// copy system file.
	const CHAR*      currentpath   = ".\\SGuardLimit_VMIO.sys";
	CHAR*            syspath       = systemMgr.sysfilePath();
	FILE*            fp;

	fp = fopen(currentpath, "rb");

	if (fp != NULL) {
		fclose(fp);
		if (!CopyFile(currentpath, syspath, FALSE)) {
			panic("CopyFile失败，你可以尝试重启SGUARD限制器或重启电脑");
		} else {
			DeleteFile(currentpath);
		}
	}

	fp = fopen(syspath, "rb");

	if (fp == NULL) {
		panic("找不到文件：SGuardLimit_VMIO.sys。这将导致MemPatch无法使用。\n请将该文件解压到同一目录下，并重启限制器。");
	} else {
		fclose(fp);
	}

	// examine system version.
	OSVersion osVersion = systemMgr.getSystemVersion();
	if (osVersion == OSVersion::OTHERS) {
		panic("MemPatch模块只支持win7和win10及以上系统。");
	}
}

void PatchManager::patch() {

	win32ThreadManager     threadMgr;
	DWORD                  pid          = threadMgr.getTargetPid();
	

	if (pid != 0         /* target exist */ &&
		pid != patchPid  /* target is not current */) {

		// before stable, wait a second.
		for (auto i = 0; patchEnabled && pid == threadMgr.getTargetPid() && i < 10; i++) {
			Sleep(1000);
		}

		// find potential target addr.
		ULONG64 rip = _findRip();
		if (rip == 0) {
			return;
		}
		
		// before manip memory, check process status for the last time.
		if (!(patchEnabled && pid == threadMgr.getTargetPid())) {
			return;
		}


		// round page.
		vmStartAddress = rip;        // %rip: near 16xx, round up.
		vmStartAddress &= ~0xfff;
		vmStartAddress -= 0x1000;


		bool             status;
		KernelDriver     driver;


		// start driver.
		status = 
		driver.load();

		if (!status) {
			panic("CreateFile失败。");
			return;
		}


		// read memory.
		status =
		driver.readVM((HANDLE)pid, vmbuf, (PVOID)vmStartAddress);

		if (!status) {
			panic("driver.readVM失败。");
			driver.unload();
			return;
		}
		memcpy(original_vm, vmbuf, 0x4000);


		// find one of syscalls to offset0.
		// offset0 starts from buffer begin.
		ULONG offset0 = 0;

		// syscall traits: 4c 8b d1 b8 ?? 00 00 00
		bool found = false;
		for (; offset0 < 0x4000 - 0x20 /* buf sz - bytes to write */; offset0++) {
			if (vmbuf[offset0] == '\x4c' &&
				vmbuf[offset0 + 1] == '\x8b' &&
				vmbuf[offset0 + 2] == '\xd1' &&
				vmbuf[offset0 + 3] == '\xb8' &&
				vmbuf[offset0 + 5] == '\x00' &&
				vmbuf[offset0 + 6] == '\x00' &&
				vmbuf[offset0 + 7] == '\x00') {
				found = true;
				break;
			}
		}

		
		static DWORD found_fail = 0;

		if (!found) {
			found_fail++;
			if (found_fail >= 5) {
				// in case sometimes it fails forever, record memory.
				char buf [128];
				sprintf(buf, "rip_%llx_vmstart_%llx.txt", rip, vmStartAddress);
				FILE* fp = fopen(buf, "w");
				for (auto i = 0; i < 0x4000; i++) {
					fprintf(fp, "%02X", (UCHAR)vmbuf[i]);
					if (i % 32 == 31) fprintf(fp, "\n");
				}
				fclose(fp);
				panic("似乎无法获取有效的内存特征，你可以把“SGUARD限制器”目录下生成的txt反馈过来，以便查看错误原因");
			}
			driver.unload();
			return;
		}

		found_fail = 0;

		// acquire system version for mutable syscall numbers.
		OSVersion osVersion = systemMgr.getSystemVersion();

		// locate offset0 to syscall 0.
		offset0 -= 0x20 * *(ULONG*)((ULONG64)vmbuf + offset0 + 4);

		// patch calls, according to switches.
		if (patchSwitches.patchDelayExecution) {
			
			LONG offset = offset0 + 0x20 * 0x34;
			CHAR patch_bytes[] = 
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x4C\x89\x12\x49\x89\xCA\xB8\x34\x00\x00\x00\x0F\x05\xC3";
			/*
				mov r10, 0xFFFFFFFFFF4143E0 ; 1250
				mov qword ptr [rdx], r10
				mov r10, rcx
				mov eax, 0x34
				syscall
				ret
			*/

			if (osVersion == OSVersion::WIN_7) {
				offset = offset0 + 0x20 * 0x31;
				patch_bytes[14] = '\x31';
			}

			LONG64 delay_param = (LONG64)-10000 * patchDelay;
			memcpy(patch_bytes + 3, &delay_param, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);
		}

		if (patchSwitches.patchResumeThread) {

			LONG offset = offset0 + 0x20 * 0x52;
			CHAR patch_bytes[] =
				"\x49\x89\xCA\xB8\x52\x00\x00\x00\x0F\x05\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A\xC3";
			/*
				mov r10, rcx
				mov eax, 0x52
				syscall
				mov r10, 0xFFFFFFFFFF4143E0
				push r10
				mov rdx, rsp
				mov eax, 0x34
				syscall
				pop r10
				ret
			*/

			if (osVersion == OSVersion::WIN_7) {
				offset = offset0 + 0x20 * 0x4f;
				patch_bytes[4] = '\x4f';
				patch_bytes[23] = '\x31';
			}

			LONG64 delay_param = (LONG64)-10000 * patchDelay;
			memcpy(patch_bytes + 13, &delay_param, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);
		}

		if (patchSwitches.patchQueryVirtualMemory) {

			LONG offset = offset0 + 0x20 * 0x23;
			CHAR patch_bytes[] =
				"\x49\x89\xCA\xB8\x23\x00\x00\x00\x0F\x05\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A\xC3";
			/*
				mov r10, rcx
				mov eax, 0x23
				syscall
				mov r10, 0xFFFFFFFFFF4143E0
				push r10
				mov rdx, rsp
				mov eax, 0x34
				syscall
				pop r10
				ret
			*/

			if (osVersion == OSVersion::WIN_7) {
				offset = offset0 + 0x20 * 0x20;
				patch_bytes[4] = '\x20';
				patch_bytes[23] = '\x31';
			}

			LONG64 delay_param = (LONG64)-10000 * patchDelay;
			memcpy(patch_bytes + 13, &delay_param, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);
		}


		// write memory.
		status = 
		driver.writeVM((HANDLE)pid, vmbuf, (PVOID)vmStartAddress);

		if (!status) {
			panic("driver.writeVM失败。");
			driver.unload();
			return;
		}

		memcpy(commited_vm, vmbuf, 0x4000);

		driver.unload();
		patchPid = pid;
	}


	while (patchEnabled) {

		pid = threadMgr.getTargetPid();

		if (pid == 0 /* target no more exists */ || pid != patchPid /* target is not current */) {
			patchPid = 0;
			break;
		}
		Sleep(5000);
	}
}

void PatchManager::enable(bool forceRecover) {
	patchEnabled = true;

	if (forceRecover) {

		win32ThreadManager threadMgr;

		DWORD pid = threadMgr.getTargetPid();

		if (patchPid != 0 && patchPid == pid) {
			KernelDriver driver;
			driver.load();
			driver.writeVM((HANDLE)patchPid, commited_vm, (PVOID)vmStartAddress);
			driver.unload();
		}
	}
}

void PatchManager::disable(bool forceRecover) {
	patchEnabled = false;

	if (forceRecover) {

		win32ThreadManager threadMgr;

		DWORD pid = threadMgr.getTargetPid();

		if (patchPid != 0 && patchPid == pid) {
			KernelDriver driver;
			driver.load();
			driver.writeVM((HANDLE)patchPid, original_vm, (PVOID)vmStartAddress);
			driver.unload();
		}
	}
}

void PatchManager::wndProcAddMenu(HMENU hMenu) {
	if (!patchEnabled) {
		AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 已撤销更改");
	} else if (g_bHijackThreadWaiting) {
		AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
	} else {
		if (patchPid == 0) { // entered memoryPatch()
			AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 请等待");
		} else {
			AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 已提交更改");
		}
	}
	AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：MemPatch V1.2 [点击切换]");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MFT_STRING, IDM_DOPATCH, "自动");
	AppendMenu(hMenu, MFT_STRING, IDM_UNDOPATCH, "撤销修改（慎用）");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH1, "patch!NtDelayExecution");
	AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH2, "patch!NtResumeThread");
	AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH3, "patch!NtQueryVirtualMemory");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	char buf[128];
	sprintf(buf, "设置延时（当前：%u）", patchDelay);
	AppendMenu(hMenu, MFT_STRING, IDM_SETDELAY, buf);
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
	if (patchEnabled) {
		CheckMenuItem(hMenu, IDM_DOPATCH, MF_CHECKED);
	} else {
		CheckMenuItem(hMenu, IDM_UNDOPATCH, MF_CHECKED);
	}
	if (patchSwitches.patchDelayExecution) {
		CheckMenuItem(hMenu, IDM_PATCHSWITCH1, MF_CHECKED);
	}
	if (patchSwitches.patchResumeThread) {
		CheckMenuItem(hMenu, IDM_PATCHSWITCH2, MF_CHECKED);
	}
	if (patchSwitches.patchQueryVirtualMemory) {
		CheckMenuItem(hMenu, IDM_PATCHSWITCH3, MF_CHECKED);
	}
}

ULONG64 PatchManager::_findRip() {

	win32ThreadManager        threadMgr;
	auto&                     threadCount    = threadMgr.threadCount;
	auto&                     threadList     = threadMgr.threadList;
	std::map<ULONG64, DWORD>  contextMap;    // rip -> visit times
	CONTEXT                   context;
	context.ContextFlags = CONTEXT_ALL;

	// open thread.
	if (!threadMgr.getTargetPid()) {
		return 0;
	}

	if (!threadMgr.enumTargetThread()) {
		return 0;
	}

	// sample 10s for cycles.
	for (auto time = 1; time <= 10; time++) {
		for (DWORD i = 0; i < threadCount; i++) {
			ULONG64 c = 0;
			QueryThreadCycleTime(threadList[i].handle, &c);
			threadList[i].cycles += c;
			threadList[i].cycleDelta = threadList[i].cycles / time;
		}
		Sleep(1000);
	}

	// sort by thread cycles in decending order.
	std::sort(threadList.begin(), threadList.end(),
		[](auto& a, auto& b) { return a.cycleDelta > b.cycleDelta; });


	// (by now) sample 1s in 1st thread, for rip abbrvt location. 
	for (auto time = 1; time <= 100; time++) {
		SuspendThread(threadList[0].handle);
		if (GetThreadContext(threadList[0].handle, &context)) {
			contextMap[context.Rip] ++;
		}
		ResumeThread(threadList[0].handle);
		Sleep(10);
	}

	if (contextMap.empty()) {
		return 0;
	}

	// return rip which has maximum counts.
	return
		std::max_element(
			contextMap.begin(), contextMap.end(),
			[](auto& a, auto& b) { return a.second < b.second; })
		->first;
}
