#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <filesystem>
#include "kdriver.h"

#define DRIVER_VERSION  "22.9.21"


// kernel-mode memory io
KernelDriver  KernelDriver::kernelDriver;

KernelDriver::KernelDriver()
	: loadFromProfileDir(true), driverReady(false), win11ForceEnable(false), win11CurrentBuild(0),
	  currentPath{}, profilePath{}, sysCurrentPath{}, sysProfilePath{}, sysfile(NULL),
	  hSCManager(NULL), hService(NULL), hDriver(INVALID_HANDLE_VALUE),
	  errorMessage_ptr(new char[0x1000]), errorCode(0), errorMessage(NULL) {
	errorMessage = errorMessage_ptr.get();
}

KernelDriver::~KernelDriver() {
	unload();
}

KernelDriver& KernelDriver::getInstance() {
	return kernelDriver;
}

bool KernelDriver::init(const std::string& currentDir, const std::string& profileDir) {

	_resetError();


	// initialize path for locating sysfile and show hint as fail occurs.
	currentPath    = currentDir;
	profilePath    = profileDir;
	sysCurrentPath = currentDir + "\\SGuardLimit_VMIO.sys";
	sysProfilePath = profileDir + "\\SGuardLimit_VMIO.sys";


	// import certificate key.
	HKEY       key;
	DWORD      dwDisposition;
	LONG       status;
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

	status = RegCreateKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\SystemCertificates\\ROOT\\Certificates\\E403A1DFC8F377E0F4AA43A83EE9EA079A1F55F2\\", 
		0, 0, 0, KEY_ALL_ACCESS, 0, &key, &dwDisposition);

	if (status == ERROR_SUCCESS) {
		if (dwDisposition == REG_CREATED_NEW_KEY) {
			status = RegSetValueEx(key, "Blob", 0, REG_BINARY, keyData, sizeof(keyData) - 1);
			if (status == ERROR_SUCCESS) {
				Sleep(1000);
			}
		}
		RegCloseKey(key);
	}

	if (status != ERROR_SUCCESS) {
		if (IDYES == MessageBox(NULL, "driver::init(): 创建注册表项失败，你可能需要手动安装证书。\n要打开证书下载页面么？", "注意", MB_YESNO)) {
			ShellExecute(0, "open", "https://bbs.colg.cn/thread-8305966-1-1.html", 0, 0, SW_SHOW);
		}
	}


	// make sysfile ready to use.
	return prepareSysfile();
}

bool KernelDriver::prepareSysfile() {

	// move sysfile in/out by config flag 'loadFromProfileDir',
	// then check sysfile's version.

	// for caller init(): call this after init()'s path initialize.
	// for caller outside: call init() first.
	
	// flag 'driverReady' is set automatically here.

	_resetError();

	bool              checkStatus   = true;
	DWORD             moveStatus    = 0;
	std::error_code   ec;

	if (loadFromProfileDir) {

		// 1. should load from profile dir:
		
		// check current dir, and move sysfile in if exists.
		if (std::filesystem::exists(sysCurrentPath, ec)) {

			if (MoveFileEx(sysCurrentPath.c_str(), sysProfilePath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				sysfile = &sysProfilePath;
			
			} else {
				sysfile = &sysProfilePath; // assign pointer to forbid crash in start service deref
				_startService(); // try stop running driver
				_endService();

				if (MoveFileEx(sysCurrentPath.c_str(), sysProfilePath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					sysfile = &sysProfilePath;

				} else {
					moveStatus  = GetLastError();
					checkStatus = false;
				}
			}
			
		} else {

			// if not exist, check profile dir.
			if (std::filesystem::exists(sysProfilePath, ec)) {
				sysfile = &sysProfilePath;

			} else {
				// file not exist at all.
				checkStatus = false;
			}
		}
	
	} else {

		// 2. should load from current dir:

		// check current dir, and load it directly if exists.
		if (std::filesystem::exists(sysCurrentPath, ec)) {
			sysfile = &sysCurrentPath;

		} else {

			// if not exist, try move sysfile out of profile dir if exists.
			if (std::filesystem::exists(sysProfilePath, ec)) {

				if (MoveFileEx(sysProfilePath.c_str(), sysCurrentPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					sysfile = &sysCurrentPath;
				
				} else {
					sysfile = &sysCurrentPath; // assign pointer to forbid crash in start service deref
					_startService(); // try stop running driver
					_endService();

					if (MoveFileEx(sysProfilePath.c_str(), sysCurrentPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
						sysfile = &sysCurrentPath;

					} else {
						moveStatus  = GetLastError();
						checkStatus = false;
					}
				}

			} else {
				// file not exist at all.
				checkStatus = false;
			}
		}
	}


	// check if sysfile is ready to load.
	if (checkStatus) {

		// sysfile is ready, check version to forbid unknown fault.
		return driverReady = _checkSysVersion();
	
	} else {

		// sysfile not ready, record error.
		_recordError(moveStatus,
			"driver::prepareSysfile(): %s。\n\n"
			"【解决办法】重启电脑再重新下载解压；若还不行，则先禁用defender，然后把以下2个目录加入杀毒信任区（如有杀毒），再重新下载解压。\n\n"
			"1. %s\n2. %s\n\n%s",
			moveStatus ? "移动sys文件失败" : "找不到sys文件：“SGuardLimit_VMIO.sys”",
			currentPath.c_str(),
			profilePath.c_str(),
			moveStatus ? "" : "【提示】把限制器和附带的sys文件解压到一起再运行，不要直接在压缩包里点开。解压目录不要包含特殊符号。");

		return driverReady = false;
	}
}

bool KernelDriver::load() {

	_resetError();

	if (hDriver == INVALID_HANDLE_VALUE) {

		if (!_startService()) {
			return false;
		}

		hDriver = CreateFile("\\\\.\\SGuardLimit_VMIO", GENERIC_ALL, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		
		if (hDriver == INVALID_HANDLE_VALUE) {
			_recordError(GetLastError(), "driver::load(): CreateFile失败。");
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
	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	for (auto page = 0; page < 4; page++) {

		request.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_READ, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
			_recordError(GetLastError(), "driver::readVM(): DeviceIoControl失败。");
			return false;
		}
		if (request.errorCode != 0) {
			_recordError(request.errorCode, "driver::readVM(): from kernel: %s", request.errorFunc);
			return false;
		}

		memcpy((PVOID)((ULONG64)out + page * 0x1000), request.data, 0x1000);
	}

	return true;
}

bool KernelDriver::writeVM(DWORD pid, PVOID in, PVOID targetAddress) {

	// assert: "in" is a 16K buffer.
	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	for (auto page = 0; page < 4; page++) {

		request.address = (PVOID)((ULONG64)targetAddress + page * 0x1000);

		memcpy(request.data, (PVOID)((ULONG64)in + page * 0x1000), 0x1000);

		if (!DeviceIoControl(hDriver, VMIO_WRITE, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
			_recordError(GetLastError(), "driver::writeVM(): DeviceIoControl失败。");
			return false;
		}
		if (request.errorCode != 0) {
			_recordError(request.errorCode, "driver::writeVM(): from kernel: %s", request.errorFunc);
			return false;
		}
	}

	return true;
}

bool KernelDriver::allocVM(DWORD pid, PVOID* pAllocatedAddress) {

	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	if (!DeviceIoControl(hDriver, VMIO_ALLOC, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::allocVM(): DeviceIoControl失败。");
		return false;
	}
	if (request.errorCode != 0) {
		_recordError(request.errorCode, "driver::allocVM(): from kernel: %s", request.errorFunc);
		return false;
	}

	*pAllocatedAddress = request.address;

	return true;
}

bool KernelDriver::suspend(DWORD pid) {

	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	if (!DeviceIoControl(hDriver, IO_SUSPEND, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::suspend(): DeviceIoControl失败。");
		return false;
	}
	if (request.errorCode != 0) {
		_recordError(request.errorCode, "driver::suspend(): from kernel: %s", request.errorFunc);
		return false;
	}

	return true;
}

bool KernelDriver::resume(DWORD pid) {

	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	if (!DeviceIoControl(hDriver, IO_RESUME, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::resume(): DeviceIoControl失败。");
		return false;
	}
	if (request.errorCode != 0) {
		_recordError(request.errorCode, "driver::resume(): from kernel: %s", request.errorFunc);
		return false;
	}

	return true;
}

bool KernelDriver::searchVad(DWORD pid, std::vector<ULONG64>& out, const wchar_t* moduleName) {

	VMIO_REQUEST  request(pid);
	DWORD         Bytes;

	_resetError();


	wcscpy((wchar_t*)request.data, moduleName);  // [io param] moduleName => (wchar_t*)request.data

	if (!DeviceIoControl(hDriver, VM_VADSEARCH, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::searchVad(): DeviceIoControl失败。");
		return false;
	}
	if (request.errorCode != 0) {
		_recordError(request.errorCode, "driver::searchVad(): from kernel: %s", request.errorFunc);
		return false;
	}

	out.clear();
	auto addressArray = (ULONG64*)request.data;

	while ( *addressArray ) {

		out.push_back(*addressArray);
		out.push_back(*(addressArray + 1));

		addressArray += 2;
	}

	return true;
}

bool KernelDriver::restoreVad() {

	VMIO_REQUEST  request(0);
	DWORD         Bytes;

	_resetError();


	if (!DeviceIoControl(hDriver, VM_VADRESTORE, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::restoreVad(): DeviceIoControl失败。");
		return false;
	}
	
	return true;
}



#define SVC_ERROR_EXIT(errorCode, errorMsg)   _recordError(errorCode, errorMsg); \
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
		SVC_ERROR_EXIT(GetLastError(), "OpenSCManager失败。");
	}

	// open Service.
	hService = OpenService(hSCManager, "SGuardLimit_VMIO", SERVICE_ALL_ACCESS);

	if (!hService) {
		hService = 
		CreateService(hSCManager, "SGuardLimit_VMIO", "SGuardLimit_VMIO",
			SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
			sysfile->c_str(),  /* no quote here, msdn e.g. is wrong */ /* assert path is valid */
			NULL, NULL, NULL, NULL, NULL);

		if (!hService) {
			SVC_ERROR_EXIT(GetLastError(), "CreateService失败。");
		}
	}

	// check service status. (return val is ignored here)
	(void)QueryServiceStatus(hService, &svcStatus);

	// if service is running, stop it.
	if (svcStatus.dwCurrentState != SERVICE_STOPPED && svcStatus.dwCurrentState != SERVICE_STOP_PENDING) {
		if (!ControlService(hService, SERVICE_CONTROL_STOP, &svcStatus)) {
			DWORD errorCode = GetLastError();
			DeleteService(hService);
			SVC_ERROR_EXIT(errorCode, "无法停止当前服务。重启电脑应该能解决该问题。");
		}
	}

	// if service is stopping,
	// wait till it's completely stopped. 
	if (svcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
		for (auto time = 0; time < 50; time++) {
			Sleep(100);
			(void)QueryServiceStatus(hService, &svcStatus);
			if (svcStatus.dwCurrentState == SERVICE_STOPPED) {
				break;
			}
		}

		if (svcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
			DeleteService(hService);
			SVC_ERROR_EXIT(0, "停止当前服务时的等待时间过长。重启电脑应该能解决该问题。");
		}
	}

	// start service.
	// if start failed, delete old one (cause old service may have wrong params)
	if (!StartService(hService, 0, NULL)) {
		DWORD errorCode = GetLastError();
		DeleteService(hService);
		SVC_ERROR_EXIT(errorCode, "StartService失败。建议查看常见问题文档。");
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

bool KernelDriver::_checkSysVersion() {

	// determine whether sysfile version matches.
	// this function don't need load() & unload(). caller should call this directly.

	_resetError();

	VMIO_REQUEST  request(0);
	DWORD         Bytes;

	if (!this->load()) {
		return false;
	}

	if (!DeviceIoControl(hDriver, VMIO_VERSION, &request, sizeof(request), &request, sizeof(request), &Bytes, NULL)) {
		_recordError(GetLastError(), "driver::_checkSysVersion(): DeviceIoControl失败。");
		this->unload();
		return false;
	}

	this->unload();

	if (0 != strcmp(request.data, DRIVER_VERSION)) {
		_recordError(0, "driver::checkSysVersion(): 内核驱动文件“SGuardLimit_VMIO.sys”不是最新的。\n\n"
			"【提示】需要把限制器和附带的sys文件解压到一起再运行，不要直接在压缩包里点开。");
		return false;
	}

	return true;
}

void KernelDriver::_resetError() {
	errorCode = 0;
	errorMessage_ptr.get()[0] = '\0';
}

void KernelDriver::_recordError(DWORD errorCode, const char* msg, ...) {
	
	this->errorCode = errorCode;
	
	va_list arg;
	va_start(arg, msg);
	vsprintf(errorMessage_ptr.get(), msg, arg);
	va_end(arg);
}