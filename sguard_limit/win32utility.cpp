#include <Windows.h>
#include <tlhelp32.h>
#include <time.h>
#include <thread>
#include <memory>
#include <filesystem>
#include "win32utility.h"


// win32Thread
win32Thread::win32Thread(DWORD tid, DWORD desiredAccess)
	: tid(tid), handle(NULL), cycles(0), cycleDelta(0), cycleDeltaAvg(0), _refCount(new DWORD(1)) {
	if (tid != 0) {
		handle = OpenThread(desiredAccess, FALSE, tid);
	}
}

win32Thread::~win32Thread() {
	// if dtor is called, _refCount is guaranteed to be valid.
	if (-- *_refCount == 0) {
		delete _refCount;
		if (handle) {
			CloseHandle(handle);
		}
	}
}

win32Thread::win32Thread(const win32Thread& t)
	: tid(t.tid), handle(t.handle), cycles(t.cycles), cycleDelta(t.cycleDelta), cycleDeltaAvg(t.cycleDeltaAvg), _refCount(t._refCount) {
	++ *_refCount;
}

win32Thread::win32Thread(win32Thread&& t) noexcept : win32Thread(0) {
	_mySwap(*this, t);
}

win32Thread& win32Thread::operator= (win32Thread t) noexcept {
	_mySwap(*this, t); /* copy & swap */ /* NRVO: optimize for both by-value and r-value */
	return *this;
}

void win32Thread::_mySwap(win32Thread& t1, win32Thread& t2) {
	std::swap(t1.tid, t2.tid);
	std::swap(t1.handle, t2.handle);
	std::swap(t1.cycles, t2.cycles);
	std::swap(t1.cycleDelta, t2.cycleDelta);
	std::swap(t1.cycleDeltaAvg, t2.cycleDeltaAvg);
	std::swap(t1._refCount, t2._refCount);
}


// win32ThreadManager
win32ThreadManager::win32ThreadManager() 
	: pid(0), threadCount(0), threadList{} {}

DWORD win32ThreadManager::getTargetPid(const char* procName) {  // ret == 0 if no proc.

	HANDLE            hSnapshot    = NULL;
	PROCESSENTRY32    pe           = {};
	pe.dwSize = sizeof(PROCESSENTRY32);


	pid = 0;

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	for (BOOL next = Process32First(hSnapshot, &pe); next; next = Process32Next(hSnapshot, &pe)) {
		if (_strcmpi(pe.szExeFile, procName) == 0) {
			pid = pe.th32ProcessID;
			break; // assert: only 1 pinstance.
		}
	}

	CloseHandle(hSnapshot);

	return pid;
}

bool win32ThreadManager::killTarget() { // kill process: return true if killed.

	if (pid == 0) {
		return false;
	}

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, pid);

	if (!hProc) {
		return false;
	}

	if (!TerminateProcess(hProc, 0)) { // async if handle is not this program.
		CloseHandle(hProc);
		return false;
	}

	WaitForSingleObject(hProc, INFINITE);
	
	CloseHandle(hProc);
	return true;
}

bool win32ThreadManager::enumTargetThread(DWORD desiredAccess) { // => threadList & threadCount

	HANDLE            hSnapshot   = NULL;
	THREADENTRY32     te          = {};
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
	: hInstance(NULL), hProgram(NULL), hWnd(NULL),
	  osVersion(OSVersion::OTHERS), osBuildNum(0), logfp(NULL), icon{}, currentDir{}, profileDir{} {}

win32SystemManager::~win32SystemManager() {

	if (logfp) {
		fclose(logfp);
	}

	if (hProgram) {
		CloseHandle(hProgram);
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
			SetProcessDPIAware();
		}

		FreeLibrary(hUser32);
	}
}

bool win32SystemManager::systemInit(HINSTANCE hInstance) {

	this->hInstance      = hInstance;


	// decide whether it's single instance.
	hProgram = CreateMutex(NULL, FALSE, "sguard_limit");
	if (!hProgram || GetLastError() == ERROR_ALREADY_EXISTS) {
		panic(0, "同时只能运行一个SGUARD限制器。");
		return false;
	}


	// initialize path vars.
	CHAR     buf [0x1000]  = {};
	DWORD    size          = 0x1000;

	GetModuleFileName(NULL, buf, size);
	if (auto p = strrchr(buf, '\\')) {
		*p = '\0';
		currentDir = buf;
	} else {
		panic("获取当前目录失败。");
		return false;
	}

	if (ExpandEnvironmentStrings("%appdata%\\sguard_limit", buf, size)) {
		profileDir = buf;
	} else {
		panic("获取系统用户目录失败。");
		return false;
	}
	

	// initialize profile directory.
	std::error_code ec;

	if (!std::filesystem::is_directory(profileDir, ec)) {
		if (!std::filesystem::create_directory(profileDir, ec)) {
			if (!std::filesystem::create_directory(profileDir = "C:\\sguard_limit", ec)) {
				panic(ec.value(), "创建用户数据目录失败。");
				return false;
			}
		}
	}


	// initialize log subsystem.
	auto      logfile       = profileDir + "\\log.txt";
	DWORD     logfileSize   = GetCompressedFileSize(logfile.c_str(), NULL);

	if (logfileSize != INVALID_FILE_SIZE && logfileSize > (1 << 16)) { // 64KB
		DeleteFile(logfile.c_str());
	}

	logfp = fopen(logfile.c_str(), "a+");

	if (!logfp) {
		panic("打开log文件%s失败。", logfile.c_str());
		return false;
	}

	setbuf(logfp, NULL);

	time_t t = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "============ session start: [%d-%02d-%02d %02d:%02d:%02d] =============\n",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);


	// acquire system version.
	// ntdll is loaded for sure, and we don't need to (neither cannot) free it.
	HMODULE hNtdll = GetModuleHandle("ntdll.dll");

	if (hNtdll) {

		typedef NTSTATUS(WINAPI* pf)(OSVERSIONINFOEX*);
		pf RtlGetVersion = (pf)GetProcAddress(hNtdll, "RtlGetVersion");

		if (RtlGetVersion) {

			OSVERSIONINFOEX osInfo;
			osInfo.dwOSVersionInfoSize = sizeof(osInfo);

			RtlGetVersion(&osInfo);

			if (osInfo.dwMajorVersion == 10) {
				osVersion = OSVersion::WIN_10_11;  // NT 10.0
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {
				osVersion = OSVersion::WIN_7;      // NT 6.1
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2) {
				osVersion = OSVersion::WIN_8;      // NT 6.2
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) {
				osVersion = OSVersion::WIN_81;     // NT 6.3
			}  // else default to:  OSVersion::OTHERS

			osBuildNum = osInfo.dwBuildNumber;
		}
	}


	return true;
}

bool win32SystemManager::enableDebugPrivilege() {

	HANDLE hToken;
	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// raise to debug previlege
	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
	
	// check if debug previlege is acquired
	if (GetLastError() != ERROR_SUCCESS) {
		panic("提升权限失败，请右键管理员运行。");

		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return true;
}

bool win32SystemManager::createWindow(WNDPROC WndProc, DWORD WndIcon) {
	
	if (!_registerMyClass(WndProc, WndIcon)) {
		panic("创建窗口类失败。");
		return false;
	}

	hWnd = CreateWindow(
		"SGuardLimit_WindowClass",
		"SGuardLimit_Window",
		WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, 0, 0, hInstance, 0);

	if (!hWnd) {
		panic("创建窗口失败。");
		return false;
	}

	ShowWindow(hWnd, SW_HIDE);

	return true;
}

WPARAM win32SystemManager::messageLoop() {
	
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

void win32SystemManager::createTray(UINT trayActiveMsg) {

	icon.cbSize = sizeof(icon);
	icon.hWnd = hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = trayActiveMsg;
	icon.hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
	strcpy(icon.szTip, "SGuard限制器");

	Shell_NotifyIcon(NIM_ADD, &icon);
}

void win32SystemManager::removeTray() {
	Shell_NotifyIcon(NIM_DELETE, &icon);
}

void win32SystemManager::log(const char* format, ...) {

	CHAR logbuf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(logbuf, format, arg);
	va_end(arg);

	_log(0, logbuf);
}

void win32SystemManager::log(DWORD errorCode, const char* format, ...) {
	
	CHAR logbuf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(logbuf, format, arg);
	va_end(arg);

	_log(errorCode, logbuf);
}

void win32SystemManager::panic(const char* format, ...) {
	
	// call GetLastError first; to avoid errors in current function.
	DWORD errorCode = GetLastError();

	CHAR buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_panic(errorCode, buf);
}

void win32SystemManager::panic(DWORD errorCode, const char* format, ...) {

	CHAR buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_panic(errorCode, buf);
}

const std::string& win32SystemManager::getCurrentDir() {
	return currentDir;
}

const std::string& win32SystemManager::getProfileDir() {
	return profileDir;
}

OSVersion win32SystemManager::getSystemVersion() {
	return osVersion;
}

DWORD win32SystemManager::getSystemBuildNum() {
	return osBuildNum;
}

void win32SystemManager::raiseCleanThread() {

	std::thread cleanThread([this] () {

		// clean thread: 1 min after game starts, end "ace-loader" process.
		// [note] former thread(s) will give up cleaning if game has re-launched.
		log("clean thread %u: created.", GetCurrentThreadId());

		win32ThreadManager  threadMgr;
		DWORD               pid            = threadMgr.getTargetPid();
		DWORD               timeElapsed    = 0;
		constexpr auto      timeToWait     = 60;

		if (pid) {

			// ensure SG pid not changed before we eliminate ace-loader,
			// here we use pid change to identify game re-launch.
			log("clean thread %u: 1 min wait begin.", GetCurrentThreadId());

			do {
				Sleep(5000);
				timeElapsed += 5;
			} while ( timeElapsed < timeToWait && pid == threadMgr.getTargetPid() );

			// if wait success, try kill ace-loader.
			// [note] check pid at end to ensure kill is immediately after check.
			// check pid won't execute twice at one time, no matter wait success or fail.
			if (timeElapsed >= timeToWait && pid == threadMgr.getTargetPid()) {

				// there maybe multiple procs, so do a while.
				while ( threadMgr.getTargetPid("GameLoader.exe") ) {

					if (threadMgr.killTarget()) {
						log("clean thread %u: eliminated GameLoader.exe - pid %u.", GetCurrentThreadId(), threadMgr.pid);

					} else {
						log(GetLastError(), "clean thread %u: clean GameLoader.exe failed.", GetCurrentThreadId());
						break;
					}
				}

			} else {
				log("clean thread %u: game re-launched, aborted.", GetCurrentThreadId());
			}
		}

		log("clean thread %u: exit.", GetCurrentThreadId());
	});

	cleanThread.detach();
}

ATOM win32SystemManager::_registerMyClass(WNDPROC WndProc, DWORD iconRcNum) {

	WNDCLASS wc = { 0 };

	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(iconRcNum));
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = "SGuardLimit_WindowClass";

	return RegisterClass(&wc);
}

void win32SystemManager::_log(DWORD code, const char* logbuf) {

	if (!logfp) {
		return;
	}

	CHAR result[0x1000];

	// put timestamp to result.
	time_t t = time(0);
	tm* local = localtime(&t);
	sprintf(result, "[%d-%02d-%02d %02d:%02d:%02d] ",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

	// put log format to result.
	strcat(result, logbuf);
	strcat(result, "\n");

	// write result to file.
	fprintf(logfp, "%s", result);
	
	// if code != 0, write [note] in another line. 
	if (code != 0) {

		// put timestamp to result.
		time_t t = time(0);
		tm* local = localtime(&t);
		sprintf(result, "[%d-%02d-%02d %02d:%02d:%02d]   note: error ",
			1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

		// put description to result.
		char* description = NULL;

		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL,
			code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&description, 0, NULL);

		sprintf(result + strlen(result), "(0x%x) %s\n", code, description);
		LocalFree(description);

		// write result to file.
		fprintf(logfp, "%s", result);
	}
}

void win32SystemManager::_panic(DWORD code, const char* showbuf) {

	CHAR result[0x1000];

	// before panic, log first.
	_log(code, showbuf);

	// put message to result.
	strcpy(result, showbuf);

	// if code != 0, add details in another line.
	if (code != 0) {

		char* description = NULL;

		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
			          code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&description, 0, NULL);

		sprintf(result + strlen(result), "\n\n发生的错误：(0x%x) %s", code, description);
		LocalFree(description);
	}

	MessageBox(0, result, 0, MB_OK);
}