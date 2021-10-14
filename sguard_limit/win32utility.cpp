#include <Windows.h>
#include <tlhelp32.h>
#include <UserEnv.h>
#include <time.h>
#include "wndproc.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"
#include "resource.h"

#include "win32utility.h"

// extern globals used by config functions.
extern volatile DWORD           g_Mode;
extern LimitManager&            limitMgr;
extern TraceManager&            traceMgr;
extern PatchManager&            patchMgr;


// win32Thread

win32Thread::win32Thread(DWORD tid, DWORD desiredAccess)
	: tid(tid), handle(NULL), cycles(0), cycleDelta(0), dieCount(0), _refCount(new DWORD(1)) {
	if (tid != 0) {
		handle = OpenThread(desiredAccess, FALSE, tid);
	}
}

win32Thread::~win32Thread() {
	// if dtor is called, _refCount is guaranteed to be valid.
	if (-- * _refCount == 0) {
		delete _refCount;
		if (handle) {
			CloseHandle(handle);
		}
	}
}

win32Thread::win32Thread(const win32Thread& t)
	: tid(t.tid), handle(t.handle), cycles(t.cycles), cycleDelta(t.cycleDelta), dieCount(t.dieCount), _refCount(t._refCount) {
	++* _refCount;
}

win32Thread::win32Thread(win32Thread&& t) noexcept : win32Thread() {
	_mySwap(*this, t);
}

win32Thread& win32Thread::operator= (const win32Thread& t) {
	win32Thread tmp = t;  /* copy & swap */
	_mySwap(*this, tmp);
	return *this;
}

win32Thread& win32Thread::operator= (win32Thread&& t) noexcept {
	_mySwap(*this, t);
	return *this;
}

void win32Thread::_mySwap(win32Thread& t1, win32Thread& t2) {
	std::swap(t1.tid, t2.tid);
	std::swap(t1.handle, t2.handle);
	std::swap(t1.cycles, t2.cycles);
	std::swap(t1.cycleDelta, t2.cycleDelta);
	std::swap(t1.dieCount, t2.dieCount);
	std::swap(t1._refCount, t2._refCount);
}



// win32ThreadManager

win32ThreadManager::win32ThreadManager() 
	: pid(0), threadCount(0), threadList({}) {}

DWORD win32ThreadManager::getTargetPid() {  // ret == 0 if no proc.

	HANDLE            hSnapshot = NULL;
	PROCESSENTRY32    pe = {};
	pe.dwSize = sizeof(PROCESSENTRY32);


	pid = 0;

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	for (BOOL next = Process32First(hSnapshot, &pe); next; next = Process32Next(hSnapshot, &pe)) {
		if (lstrcmpi(pe.szExeFile, "SGuard64.exe") == 0) {
			pid = pe.th32ProcessID;
			break; // assert: only 1 pinstance.
		}
	}

	CloseHandle(hSnapshot);

	return pid;
}

bool win32ThreadManager::enumTargetThread(DWORD desiredAccess) { // => threadList & threadCount

	HANDLE            hSnapshot = NULL;
	THREADENTRY32     te = {};
	te.dwSize = sizeof(THREADENTRY32);


	threadCount = 0;
	threadList.clear();


	if (pid == 0) {
		return false;
	}


	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	for (BOOL next = Thread32First(hSnapshot, &te); next; next = Thread32Next(hSnapshot, &te)) {
		if (te.th32OwnerProcessID == pid) {
			found = true;
			threadList.push_back({ te.th32ThreadID, desiredAccess });
		}
	}

	CloseHandle(hSnapshot);

	threadCount = (DWORD)threadList.size();

	return found;
}


// win32SystemManager

win32SystemManager win32SystemManager::systemManager;

win32SystemManager::win32SystemManager() 
	: hWnd(NULL), hInstance(NULL), 
	  osVersion(OSVersion::OTHERS), logfp(NULL),
	  icon({}), profileDir(), profile(), sysfile(), logfile() {}

win32SystemManager::~win32SystemManager() {
	if (logfp) {
		fclose(logfp);
	}
}

win32SystemManager& win32SystemManager::getInstance() {
	return systemManager;
}

void win32SystemManager::setupProcessDpi() {

	HMODULE hUser32 = LoadLibrary("User32.dll");

	if (hUser32) {

		typedef BOOL(WINAPI* fp)(DPI_AWARENESS_CONTEXT);
		fp SetProcessDpiAwarenessContext = (fp)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");

		if (SetProcessDpiAwarenessContext) {
			SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
		} else {

			typedef BOOL(WINAPI* fp2)();
			fp2 SetProcessDPIAware = (fp2)GetProcAddress(hUser32, "SetProcessDPIAware");
			if (SetProcessDPIAware) {
				SetProcessDPIAware();
			}
		}

		FreeLibrary(hUser32);
	}
}

void win32SystemManager::systemInit(HINSTANCE hInst) {
	
	// initialize application vars.
	hInstance = hInst;


	// initialize path vars.
	HANDLE       hToken;
	DWORD        size = 1024;

	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
	GetUserProfileDirectory(hToken, profileDir, &size);
	strcat(profileDir, "\\AppData\\Roaming\\sguard_limit");
	CloseHandle(hToken);

	strcpy(profile, profileDir);
	strcpy(sysfile, profileDir);
	strcpy(logfile, profileDir);
	strcat(profile, "\\config.ini");
	strcat(sysfile, "\\SGuardLimit_VMIO.sys");
	strcat(logfile, "\\log.txt");


	// initialize log system.
	// if old log is larger than 1MiB, delete it.
	DWORD filesize = GetCompressedFileSize(logfile, NULL);

	if (filesize > (1 << 20)) {
		DeleteFile(logfile);
	}

	// append new session sign to log.
	logfp = fopen(logfile, "a+");
	setbuf(logfp, NULL);

	time_t t = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "============ session start: [%d-%02d-%02d %02d:%02d:%02d] =============",
		1900 + local->tm_year, local->tm_mon, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
	fprintf(logfp, "\n");


	// acquire system version.
	typedef NTSTATUS(WINAPI* pf)(OSVERSIONINFOEX*);
	
	pf RtlGetVersion = (pf)GetProcAddress(GetModuleHandle("ntdll.dll"), "RtlGetVersion");

	if (RtlGetVersion) {
		OSVERSIONINFOEX osInfo;
		osInfo.dwOSVersionInfoSize = sizeof(osInfo);
		RtlGetVersion(&osInfo);

		if (osInfo.dwMajorVersion >= 10) {
			osVersion = OSVersion::WIN_10;
		} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {
			osVersion = OSVersion::WIN_7;
		} else {
			osVersion = OSVersion::OTHERS;
		}
	}
}

void win32SystemManager::enableDebugPrivilege() {

	HANDLE hToken;
	LUID Luid;
	TOKEN_PRIVILEGES tp;

	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &Luid);

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = Luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);

	CloseHandle(hToken);
}

bool win32SystemManager::checkDebugPrivilege() {

	HANDLE hToken;
	LUID luidPrivilege = { 0 };
	PRIVILEGE_SET RequiredPrivileges = { 0 };
	BOOL bResult = 0;

	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidPrivilege);

	RequiredPrivileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
	RequiredPrivileges.PrivilegeCount = 1;
	RequiredPrivileges.Privilege[0].Luid = luidPrivilege;
	RequiredPrivileges.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

	PrivilegeCheck(hToken, &RequiredPrivileges, &bResult);

	CloseHandle(hToken);

	return (bool)bResult;
}

// 另一种提权方法（使用未公开接口）
//static void Enable_se_debug() { // stdcall convention declaration can be omitted if use x64.
//	typedef int(__stdcall* pf)(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);
//	pf RtlAdjustPrivilege = (pf)GetProcAddress(GetModuleHandle("Ntdll.dll"), "RtlAdjustPrivilege");
//	BOOLEAN prev;
//	int ret = RtlAdjustPrivilege(0x14, 1, 0, &prev);
//}

bool win32SystemManager::createWin32Window(WNDPROC WndProc) {
	
	if (!_registerMyClass(WndProc)) {
		return false;
	}

	hWnd = CreateWindow(
		"SGuardLimit_WindowClass",
		"SGuardLimit_Window",
		WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, 0, 0, hInstance, 0);

	if (!hWnd) {
		return false;
	}

	ShowWindow(hWnd, SW_HIDE);

	return true;
}

WPARAM win32SystemManager::messageLoop() {
	
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

void win32SystemManager::createTray() {

	icon.cbSize = sizeof(icon);
	icon.hWnd = hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = WM_TRAYACTIVATE;
	icon.hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
	strcpy(icon.szTip, "SGuard限制器");

	Shell_NotifyIcon(NIM_ADD, &icon);
}

void win32SystemManager::removeTray() {
	Shell_NotifyIcon(NIM_DELETE, &icon);
}

bool win32SystemManager::loadConfig() {  // executes only when program is initalizing.

	char          version[128];
	bool          result = true;

	// if path does not exist, create a new one.
	DWORD pathAttr = GetFileAttributes(profileDir);
	if ((pathAttr == INVALID_FILE_ATTRIBUTES) || !(pathAttr & FILE_ATTRIBUTE_DIRECTORY)) {
		CreateDirectory(profileDir, NULL);
		result = false;
	}

	// check version.
	GetPrivateProfileString("Global", "Version", NULL, version, 128, profile);
	if (strcmp(version, VERSION) != 0) {
		WritePrivateProfileString("Global", "Version", VERSION, profile);
		result = false;
	}

	// load configurations.
	UINT res = GetPrivateProfileInt("Global", "Mode", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1 && res != 2)) {
		WritePrivateProfileString("Global", "Mode", "2", profile);
		g_Mode = 2;
	} else {
		g_Mode = res;
	}

	// limit module
	res = GetPrivateProfileInt("Limit", "Percent", -1, profile);
	if (res == (UINT)-1 || (res != 90 && res != 95 && res != 99 && res != 999)) {
		WritePrivateProfileString("Limit", "Percent", "90", profile);
		limitMgr.limitPercent = 90;
	} else {
		limitMgr.limitPercent = res;
	}

	// lock module
	res = GetPrivateProfileInt("Lock", "Mode", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1 && res != 2 && res != 3)) {
		WritePrivateProfileString("Lock", "Mode", "0", profile);
		traceMgr.lockMode = 0;
	} else {
		traceMgr.lockMode = res;
	}

	res = GetPrivateProfileInt("Lock", "Round", -1, profile);
	if (res == (UINT)-1 || (res < 1 || res > 99)) {
		WritePrivateProfileString("Lock", "Round", "95", profile);
		traceMgr.lockRound = 95;
	} else {
		traceMgr.lockRound = res;
	}

	// patch module
	res = GetPrivateProfileInt("Patch", "Delay0", -1, profile);
	if (res == (UINT)-1 || (res < 200 || res > 2000)) {
		WritePrivateProfileString("Patch", "Delay0", "1250", profile);
		patchMgr.patchDelay[0] = 1250;
	} else {
		patchMgr.patchDelay[0] = res;
	}

	res = GetPrivateProfileInt("Patch", "Delay1", -1, profile);
	if (res == (UINT)-1 || (res < 200 || res > 5000)) {
		WritePrivateProfileString("Patch", "Delay1", "2000", profile);
		patchMgr.patchDelay[1] = 2000;
	} else {
		patchMgr.patchDelay[1] = res;
	}

	res = GetPrivateProfileInt("Patch", "Delay2", -1, profile);
	if (res == (UINT)-1 || (res < 200 || res > 2000)) {
		WritePrivateProfileString("Patch", "Delay2", "1250", profile);
		patchMgr.patchDelay[2] = 1250;
	} else {
		patchMgr.patchDelay[2] = res;
	}

	res = GetPrivateProfileInt("Patch", "NtQueryVirtualMemory", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtQueryVirtualMemory", "1", profile);
		patchMgr.patchSwitches.NtQueryVirtualMemory = true;
	} else {
		patchMgr.patchSwitches.NtQueryVirtualMemory = res ? true : false;
	}

	res = GetPrivateProfileInt("Patch", "NtWaitForSingleObject", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtWaitForSingleObject", "0", profile);
		patchMgr.patchSwitches.NtWaitForSingleObject = false;
	} else {
		patchMgr.patchSwitches.NtWaitForSingleObject = res ? true : false;
	}

	res = GetPrivateProfileInt("Patch", "NtDelayExecution", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtDelayExecution", "0", profile);
		patchMgr.patchSwitches.NtDelayExecution = false;
	} else {
		patchMgr.patchSwitches.NtDelayExecution = res ? true : false;
	}

	// if it's first time user updates to this version, force to mode 2.
	if (!result) {
		g_Mode = 2;
	}

	return result;
}

void win32SystemManager::writeConfig() {

	char buf[16];

	sprintf(buf, "%u", g_Mode);
	WritePrivateProfileString("Global", "Mode", buf, profile);

	sprintf(buf, "%u", limitMgr.limitPercent);
	WritePrivateProfileString("Limit", "Percent", buf, profile);

	sprintf(buf, "%u", traceMgr.lockMode);
	WritePrivateProfileString("Lock", "Mode", buf, profile);

	sprintf(buf, "%u", traceMgr.lockRound);
	WritePrivateProfileString("Lock", "Round", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[0]);
	WritePrivateProfileString("Patch", "Delay0", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[1]);
	WritePrivateProfileString("Patch", "Delay1", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[2]);
	WritePrivateProfileString("Patch", "Delay2", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtQueryVirtualMemory ? "1" : "0");
	WritePrivateProfileString("Patch", "NtQueryVirtualMemory", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtWaitForSingleObject ? "1" : "0");
	WritePrivateProfileString("Patch", "NtWaitForSingleObject", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtDelayExecution ? "1" : "0");
	WritePrivateProfileString("Patch", "NtDelayExecution", buf, profile);
}

void win32SystemManager::log(const char* format, ...) {

	time_t t = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "[%d-%02d-%02d %02d:%02d:%02d] ", 1900 + local->tm_year, local->tm_mon, local->tm_mday,
		local->tm_hour, local->tm_min, local->tm_sec);

	va_list arg;
	va_start(arg, format);
	vfprintf(logfp, format, arg);
	va_end(arg);

	fprintf(logfp, "\n");
}

CHAR* win32SystemManager::sysfilePath() {
	return sysfile;
}

OSVersion win32SystemManager::getSystemVersion() {
	return osVersion;
}

ATOM win32SystemManager::_registerMyClass(WNDPROC WndProc) {

	WNDCLASS wc = { 0 };

	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = "SGuardLimit_WindowClass";

	return RegisterClass(&wc);
}