// Memory Patch（驱动模式）
// 2021.10.4 雨
// 2.2 复刻胡桃，开心。但是昨天吃坏肚子了，很疼。
#include <Windows.h>
#include <tlhelp32.h>
#include <UserEnv.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <map>
#include "limitcore.h"  // GetProcessID, EnumCurrentThread
#include "panic.h"

volatile bool     patchEnabled   = true;
volatile DWORD    patchPid       = 0;
volatile DWORD    patchDelay     = 1250;


// driver interface
#define VMIO_READ   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_WRITE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef struct {
	CHAR     data[4096];
	PVOID    address;
	HANDLE   pid;

	CHAR     errorFunc[128];
	ULONG    errorCode;
} VMIO_REQUEST;


// initialize
enum OS_Version_t { WIN_7, WIN_10 } OS_Version;

CHAR sysFilePath[1024];

void importCertRegKey() {

	HKEY key;
	DWORD dwDisposition;

	if (ERROR_SUCCESS != RegCreateKeyEx(
		HKEY_LOCAL_MACHINE, 
		"SOFTWARE\\Microsoft\\SystemCertificates\\ROOT\\Certificates\\E403A1DFC8F377E0F4AA43A83EE9EA079A1F55F2\\",
		0, 0, 0, KEY_ALL_ACCESS, 0, &key, &dwDisposition)) {

		panic("RegCreateKeyEx失败，你可能需要手动安装证书");
		return;
	}

	const BYTE keyData[] = 
		"\x53\x00\x00\x00\x01\x00\x00\x00\x23\x00\x00\x00\x30\x21\x30\x1f\x06\x09\x60\x86\x48\x01"
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
		"\xf2\x92\x69\x45\xcf\xbf\xf3\x2c\xaa"
		;

	if (dwDisposition == REG_CREATED_NEW_KEY) {
		if (ERROR_SUCCESS != RegSetValueEx(key, "Blob", 0, REG_BINARY, keyData, sizeof(keyData))) {
			panic("RegSetValueEx失败，你可能需要手动安装证书");
		}

		Sleep(1000);
	}

	RegCloseKey(key);

}

void copySysFile() {

	HANDLE hToken;
	
	DWORD size = 1024;
	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
	GetUserProfileDirectory(hToken, sysFilePath, &size);
	sprintf(sysFilePath + strlen(sysFilePath), "\\AppData\\Roaming\\sguard_limit\\SGuardLimit_VMIO.sys");
	CloseHandle(hToken);

	const CHAR* currentFilePath = ".\\SGuardLimit_VMIO.sys";

	FILE* fp = fopen(currentFilePath, "rb");
	if (fp != NULL) {

		fclose(fp);
		if (!CopyFile(currentFilePath, sysFilePath, FALSE)) {
			panic("CopyFile失败，你可以尝试重启SGUARD限制器或重启电脑");
		} else {
			DeleteFile(currentFilePath);
		}
		return;
	}

	fp = fopen(sysFilePath, "rb");
	if (fp != NULL) {
		
		fclose(fp);
		return;
	}

	panic("找不到文件：SGuardLimit_VMIO.sys。这将导致Memory Patch无法使用。\n请将该文件解压到同一目录下，并重启限制器。");
}

bool getSystemVersion() {

	typedef NTSTATUS(WINAPI* pf)(OSVERSIONINFOEX*);
	pf RtlGetVersion = (pf)GetProcAddress(GetModuleHandle("ntdll.dll"), "RtlGetVersion");
	
	if (!RtlGetVersion) {
		panic("获取系统版本失败！");
		return false;
	}

	OSVERSIONINFOEX osInfo;
	osInfo.dwOSVersionInfoSize = sizeof(osInfo);

	RtlGetVersion(&osInfo);

	if (osInfo.dwMajorVersion >= 10) {
		OS_Version = WIN_10;
	} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {
		OS_Version = WIN_7;
	} else {
		if (IDYES == MessageBox(0, "注意：模式Memory Patch只支持win7和win10及以上系统。你的系统不支持该功能", "信息", MB_YESNO)) {
			OS_Version = WIN_10;
			return true;
		} else {
			return false;
		}
	}

	return true;
}

bool initializePatchModule() {
	importCertRegKey();
	copySysFile();
	return getSystemVersion();
}


// driver io
HANDLE driver_Load() {

	// this part of api is complex. use cmd instead we can make code short.
	ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(100);
	ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(100);
	char arg[1024] = "create SGuardLimit_VMIO type= kernel binPath= ";
	sprintf(arg + strlen(arg), sysFilePath);
	ShellExecute(0, "open", "sc", arg, 0, SW_HIDE);
	Sleep(100);
	ShellExecute(0, "open", "sc", "start SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(500);

	return CreateFile("\\\\.\\SGuardLimit_VMIO", GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
}

void driver_Unload(HANDLE hDriver) {

	CloseHandle(hDriver);

	ShellExecute(0, "open", "sc", "stop SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(100);
	ShellExecute(0, "open", "sc", "delete SGuardLimit_VMIO", 0, SW_HIDE);
	Sleep(100);
}

bool driver_ReadVM(HANDLE hDriver, HANDLE pid, PVOID out, PVOID targetAddress) {

	// assert: "out" is a 8K buffer.
	VMIO_REQUEST  ReadRequest;
	DWORD         Bytes;

	ReadRequest.pid = pid;
	ReadRequest.errorCode = 0;


	ReadRequest.address = (PVOID)targetAddress;
	DeviceIoControl(hDriver, VMIO_READ, &ReadRequest, sizeof(ReadRequest), &ReadRequest, sizeof(ReadRequest), &Bytes, NULL);
	if (ReadRequest.errorCode != 0) {
		panic("驱动内部错误：%s(%x)", ReadRequest.errorFunc, ReadRequest.errorCode);
		return false;
	}
	memcpy(out, ReadRequest.data, 4096);


	ReadRequest.address = (PVOID)((ULONG64)targetAddress + 4096);
	DeviceIoControl(hDriver, VMIO_READ, &ReadRequest, sizeof(ReadRequest), &ReadRequest, sizeof(ReadRequest), &Bytes, NULL);
	if (ReadRequest.errorCode != 0) {
		panic("驱动内部错误：%s(%x)", ReadRequest.errorFunc, ReadRequest.errorCode);
	}
	memcpy((PVOID)((ULONG64)out + 4096), ReadRequest.data, 4096);


	return true;
}

bool driver_WriteVM(HANDLE hDriver, HANDLE pid, PVOID in, PVOID targetAddress) {
	
	// assert: "in" is a 8K buffer.
	VMIO_REQUEST  WriteRequest;
	DWORD         Bytes;

	WriteRequest.pid = pid;
	WriteRequest.errorCode = 0;


	WriteRequest.address = (PVOID)targetAddress;
	memcpy(WriteRequest.data, in, 4096);
	DeviceIoControl(hDriver, VMIO_WRITE, &WriteRequest, sizeof(WriteRequest), &WriteRequest, sizeof(WriteRequest), &Bytes, NULL);
	if (WriteRequest.errorCode != 0) {
		panic("驱动内部错误：%s(0x%x)", WriteRequest.errorFunc, WriteRequest.errorCode);
		return false;
	}


	WriteRequest.address = (PVOID)((ULONG64)targetAddress + 4096);
	memcpy(WriteRequest.data, (PVOID)((ULONG64)in + 4096), 4096);
	DeviceIoControl(hDriver, VMIO_WRITE, &WriteRequest, sizeof(WriteRequest), &WriteRequest, sizeof(WriteRequest), &Bytes, NULL);
	if (WriteRequest.errorCode != 0) {
		panic("驱动内部错误：%s(0x%x)", WriteRequest.errorFunc, WriteRequest.errorCode);
		return false;
	}


	return true;
}


// controller
ULONG64 vmStartAddress = 0;
CHAR original_vm[8192] = {};
CHAR commited_vm[8192] = {};

void enablePatch() {
	patchEnabled = true;
	if (patchPid != 0 && patchPid == GetProcessID()) {
		HANDLE hDriver = driver_Load();
		driver_WriteVM(hDriver, (HANDLE)patchPid, commited_vm, (PVOID)vmStartAddress);
		driver_Unload(hDriver);
	}
}

void disablePatch() {
	patchEnabled = false;
	if (patchPid != 0 && patchPid == GetProcessID()) {
		HANDLE hDriver = driver_Load();
		driver_WriteVM(hDriver, (HANDLE)patchPid, original_vm, (PVOID)vmStartAddress);
		driver_Unload(hDriver);
	}
}


// todo: wrap in struct.
extern DWORD  threadIDList[512];
extern DWORD  numThreads;


void memoryPatch(DWORD pid) {
	
	if (patchPid != pid) {

		vmStartAddress = 0;
		memset(original_vm, 0, 8192);
		memset(commited_vm, 0, 8192);

		// wait a second..
		for (auto i = 0; patchEnabled && i < 30; i++) {
			if (pid != GetProcessID()) {
				return;
			}
			Sleep(1000);
		}

		// open target handle now.
		HANDLE threadHandleList[100];
		numThreads = 0;
		memset(threadIDList, 0, sizeof(threadIDList));
		memset(threadHandleList, 0, sizeof(threadHandleList));

		if (!EnumCurrentThread(pid)) {
			return;
		}

		for (auto i = 0; i < numThreads; i++) {
			threadHandleList[i] = OpenThread(THREAD_ALL_ACCESS, 0, threadIDList[i]);
		}

		// find rip.
		struct {DWORD tid;HANDLE handle;} targets[3];
		{
			struct {
				DWORD tid;
				HANDLE handle;
				ULONG64 cycles;
				ULONG64 dcycles;
			} all_threads[100];

			for (auto i = 0; i < numThreads; i++) {
				all_threads[i].tid = threadIDList[i];
				all_threads[i].handle = threadHandleList[i];
				all_threads[i].cycles = 0;
				all_threads[i].dcycles = 0;
			}

			for (auto a = 1; a < 100; a++) {
				for (auto i = 0; i < numThreads; i++) {
					ULONG64 c = 0;
					QueryThreadCycleTime(threadHandleList[i], &c);
					all_threads[i].cycles += c;
					all_threads[i].dcycles = all_threads[i].cycles / a;
				}
				Sleep(5);
			}

			std::sort(all_threads, all_threads + numThreads, [](auto& a, auto& b) {
				return a.dcycles > b.dcycles;
				});

			for (auto i = 0; i < 3; i++) {
				targets[i].tid = all_threads[i].tid;
				targets[i].handle = all_threads[i].handle;
			}
		}
		std::map< ULONG64, int > contextMap[3]; // for each thread, instruction address ->  visit times
		CONTEXT context;
		context.ContextFlags = CONTEXT_ALL;

		for (auto i = 0; i < 100; i++) {
			for (auto i = 0; i < 3; i++) {
				SuspendThread(targets[i].handle);
				if (GetThreadContext(targets[i].handle, &context)) {
					contextMap[i][context.Rip] ++;
				}
				ResumeThread(targets[i].handle);
			}
			Sleep(5);
		}
		int max = 0;
		ULONG64 rip;
		for (auto i = 0; i < 3; i++) {
			for (auto it = contextMap[i].begin(); it != contextMap[i].end(); it++) {
				if (max < it->second) {
					max = it->second;
					rip = it->first;
				}
			}
		}

		for (auto i = 0; i < numThreads; i++) {
			CloseHandle(threadHandleList[i]);
		}

		if (!(patchEnabled && pid == GetProcessID())) {
			return;
		}


		// find virtual page.
		ULONG64 rwBeginAddress = rip;
		rwBeginAddress &= ~0xfff;
		rwBeginAddress -= 0x1000;

		vmStartAddress = rwBeginAddress;

		// start driver.
		HANDLE hDriver = driver_Load();

		if (hDriver == INVALID_HANDLE_VALUE) {
			panic("CreateFile失败。", GetLastError());
			return;
		}

		static CHAR vmbuf[8192];
		memset(vmbuf, 0, 8192);
		bool found_rip = false;

		// load pages around rip.
		for (auto bias = 0; bias < 4; bias++) {

			bias % 2 ? rwBeginAddress += 0x1000 * bias : rwBeginAddress -= 0x1000 * bias;


			// read memory.
			if (!driver_ReadVM(hDriver, (HANDLE)pid, vmbuf, (PVOID)rwBeginAddress)) { 
				panic("driver_ReadVM失败。");
				driver_Unload(hDriver);
				return;
			}
			memcpy(original_vm, vmbuf, 8192);


			// find mem trait (@Ntdll!NtDelayExecution)
			ULONG64 offset = 0;
			bool found = false;

			if (OS_Version == WIN_10) {
				for (; offset < 8192 - 32 /* buf sz - bytes to write */; offset++) {
					if (vmbuf[offset] == '\x4c' &&
						vmbuf[offset + 1] == '\x8b' &&
						vmbuf[offset + 2] == '\xd1' &&
						vmbuf[offset + 3] == '\xb8' &&
						vmbuf[offset + 4] == '\x34' &&
						vmbuf[offset + 5] == '\x00' &&
						vmbuf[offset + 6] == '\x00' &&
						vmbuf[offset + 7] == '\x00') {
						found = true;
						break;
					}
				}
			} else { // if WIN_7
				for (; offset < 8192 - 32 /* buf sz - bytes to write */; offset++) {
					if (vmbuf[offset] == '\x4c' &&
						vmbuf[offset + 1] == '\x8b' &&
						vmbuf[offset + 2] == '\xd1' &&
						vmbuf[offset + 3] == '\xb8' &&
						vmbuf[offset + 4] == '\x31' &&
						vmbuf[offset + 5] == '\x00' &&
						vmbuf[offset + 6] == '\x00' &&
						vmbuf[offset + 7] == '\x00') {
						found = true;
						break;
					}
				}
			}
			
			if (!found) {
				continue;
			}

			/*
				mov r10, 0xFFFFFFFFFF4143E0 ; 1250
				mov qword ptr [rdx], r10
				mov r10, rcx
				mov eax, 0x34
				syscall
				ret
			*/
			CHAR patch_bytes[] =
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x4C\x89\x12\x49\x89\xCA\xB8\x34\x00\x00\x00\x0F\x05\xC3";
			
			if (OS_Version == WIN_7) {
				patch_bytes[14] = '\x31';
			}

			LONG64 delay_param = (LONG64)-10000 * patchDelay;
			memcpy(patch_bytes + 3, &delay_param, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes));


			// write back.
			if (!driver_WriteVM(hDriver, (HANDLE)pid, vmbuf, (PVOID)rwBeginAddress)) {
				panic("driver_WriteVM失败。");
				driver_Unload(hDriver);
				return;
			}

			memcpy(commited_vm, vmbuf, 8192);

			driver_Unload(hDriver);
			found_rip = true;
			break;
		}

		if (!found_rip) {
			driver_Unload(hDriver);
			return;
		}

		patchPid = pid;
	}

	while (patchEnabled) {
		Sleep(5000);
		if (patchPid != GetProcessID()) {
			patchPid = 0;
			return;
		}
	}
}