#include <Windows.h>
#include <Shlobj.h>
#include <tlhelp32.h>
#include <time.h>
#include <thread>
#include <atomic>
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
	: autoStartup(false), killAceLoader(true), hInstance(NULL), hProgram(NULL), hWnd(NULL),
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

bool win32SystemManager::runWithUac() {

	if (!IsUserAnAdmin()) {

		char    path        [0x1000];
		DWORD   errorCode   = 0;

		GetModuleFileName(NULL, path, 0x1000);
		errorCode = (DWORD)(INT_PTR)
		ShellExecute(NULL, "runas", path, NULL /* no cmdline here */, NULL, SW_SHOWNORMAL);
		
		if (errorCode <= 32) {
			panic(errorCode, "�޷���uacȨ��������·�����Ƿ����������ţ�");
		}

		return false;
	
	} else {
		return true;
	}
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
		panic(0, "ͬʱֻ������һ��SGUARD��������");
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
		panic("��ȡ��ǰĿ¼ʧ�ܡ�");
		return false;
	}

	if (ExpandEnvironmentStrings("%appdata%\\sguard_limit", buf, size)) {
		profileDir = buf;
	} else {
		panic("��ȡϵͳ�û�Ŀ¼ʧ�ܡ�");
		return false;
	}
	

	// initialize profile directory.
	std::error_code ec;

	if (!std::filesystem::is_directory(profileDir, ec)) {
		if (!std::filesystem::create_directory(profileDir, ec)) {
			if (!std::filesystem::create_directory(profileDir = "C:\\sguard_limit", ec)) {
				panic(ec.value(), "�����û�����Ŀ¼ʧ�ܡ�");
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
		panic("��log�ļ�%sʧ�ܡ�", logfile.c_str());
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
		panic("����Ȩ��ʧ�ܣ����Ҽ�����Ա���С�");

		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return true;
}

bool win32SystemManager::createWindow(WNDPROC WndProc, DWORD WndIcon) {
	
	if (!_registerMyClass(WndProc, WndIcon)) {
		panic("����������ʧ�ܡ�");
		return false;
	}

	hWnd = CreateWindow(
		"SGuardLimit_WindowClass",
		"SGuardLimit_Window",
		WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, 0, 0, hInstance, 0);

	if (!hWnd) {
		panic("��������ʧ�ܡ�");
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
	strcpy(icon.szTip, "SGuard������");

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

bool win32SystemManager::modifyStartupReg() {

	HKEY   hKey;
	bool   ret    = true;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
		
		if (autoStartup) {
			// should auto start: create key.
			char path[0x1000];
			GetModuleFileName(NULL, path, 0x1000);
			if (RegSetValueEx(hKey, "sguard_limit", 0, REG_SZ, (const BYTE*)path, strlen(path) + 1) != ERROR_SUCCESS) {
				panic("modifyStartupReg(): RegSetValueExʧ�ܡ�");
				ret = false;
			}
		
		} else {
			// should not auto start: remove key.
			// if key doesn't exist, will return fail. ignore it.
			RegDeleteValue(hKey, "sguard_limit");
		}
		
		RegCloseKey(hKey);

	} else {
		panic("modifyStartupReg(): RegOpenKeyExʧ�ܡ�");
		ret = false;
	}

	return ret;
}

void win32SystemManager::raiseCleanThread() {

	std::thread cleanThread([this] () {

		DWORD tid = GetCurrentThreadId();

		// check if clean thread already exist. in that case exit.
		static std::atomic<DWORD> lock = 0;
		DWORD expected = 0;
		
		// [note] make atomic operation at instruction level (e.g. x86 lock cmpxchg)
		// is more fast than sync with mutex which may trap in kernel,
		// because cpu cache lock only affect single cache line.
		// see: https://stackoverflow.com/questions/2538070/atomic-operation-cost
		if (lock.compare_exchange_strong(expected, tid)) {
			log("clean thread %u: lock acquired.", tid);

		} else {
			log("clean thread %u: lock is now held by %u, exiting.", tid, lock.load());
			return;
		}


		// wait 60 secs after game (SG) start to ensure it's stable to clean.
		// if game not exist, still wait 60 secs and make clean.
		win32ThreadManager  threadMgr;
		DWORD               pid            = threadMgr.getTargetPid();
		DWORD               timeElapsed    = 0;
		constexpr auto      timeToWait     = 60;

		while (timeElapsed < timeToWait) {

			Sleep(5000);
			timeElapsed += 5;

			// every 5 secs, check SG's instance.
			// if one of (SG not exist || SG pid alive) keeps 60 secs, kill ace-loader.
			auto pidNow = threadMgr.getTargetPid();

			// if pid changed (both pid == 0 or != 0), reset timer.
			if (pidNow != pid) {
				log("clean thread %u: SG pid changed to %u, reset timer.", tid, pidNow);
				pid = pidNow;
				timeElapsed = 0;
			}
		};


		// start clean ace-loader process.
		// there maybe multiple procs, so do a while.
		while (threadMgr.getTargetPid("GameLoader.exe")) {

			if (threadMgr.killTarget()) {
				log("clean thread %u: eliminated GameLoader.exe - pid %u.", tid, threadMgr.pid);

			} else {
				log(GetLastError(), "clean thread %u: clean GameLoader.exe failed.", tid);
			}
		}


		// release lock and exit.
		log("clean thread %u: exit and release lock.", tid);
		lock = 0;
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

		sprintf(result + strlen(result), "\n\n�����Ĵ���(0x%x) %s", code, description);
		LocalFree(description);
	}

	MessageBox(0, result, 0, MB_OK);
}