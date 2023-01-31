#include <Windows.h>
#include <Shlobj.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <time.h>
#include <thread>
#include <filesystem>
#include <cjson/cJSON.h>
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
	: cloudDataReady(false), cloudVersion{}, cloudVersionDetail{},
	  cloudUpdateLink{}, cloudShowNotice{}, cloudBanList{},
	  autoStartup(false), autoCheckUpdate(false), killAceLoader(true), 
	  hInstance(NULL), hWnd(NULL), hProgram(NULL),
	  profileDir{}, osVersion(OSVersion::OTHERS), osBuildNum(0), 
	  logfp(NULL), icon{} {}

win32SystemManager::~win32SystemManager() {

	if (logfp) {
		fclose(logfp);
	}

	if (hProgram) {
		ReleaseMutex(hProgram);
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
			panic(errorCode, "无法以uac权限启动，路径中是否包含特殊符号？");
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

	this->hInstance = hInstance;


	// decide whether it's single instance.
	hProgram = CreateMutex(NULL, FALSE, "sguard_limit");
	if (!hProgram || GetLastError() == ERROR_ALREADY_EXISTS) {
		panic(0, "同时只能运行一个SGUARD限制器。");
		return false;
	}


	// initialize path vars.
	char profilePath[0x1000] = {};
	if (ExpandEnvironmentStrings("%appdata%\\sguard_limit", profilePath, 0x1000)) {
		profileDir = profilePath;
	} else {
		panic("获取系统用户目录失败。");
		return false;
	}


	// initialize profile directory.
	std::error_code ec;

	if (!std::filesystem::is_directory(profileDir, ec)) {
		if (!std::filesystem::create_directory(profileDir, ec)) {
			if (!std::filesystem::is_directory(profileDir = "C:\\sguard_limit", ec)) {
				if (!std::filesystem::create_directory(profileDir, ec)) {
					panic(ec.value(), "创建用户数据目录失败。");
					return false;
				}
			}
		}
	}


	// initialize log subsystem.
	auto      logfile = profileDir + "\\log.txt";
	DWORD     logfileSize = GetCompressedFileSize(logfile.c_str(), NULL);

	if (logfileSize != INVALID_FILE_SIZE && logfileSize > (1 << 15)) { // 32KB
		DeleteFile(logfile.c_str());
	}

	logfp = fopen(logfile.c_str(), "a+");

	if (!logfp) {
		panic("打开log文件失败。");
		return false;
	}

	setbuf(logfp, NULL);

	time_t t = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "============ session start: [%d-%02d-%02d %02d:%02d:%02d] =============\n",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);


	// acquire system version.
	// ntdll is loaded for sure, and we don't need to (neither cannot) free it.
	if (auto hNtdll = GetModuleHandle("Ntdll.dll")) {

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

			log("systemInit(): Running on Windows NT %u.%u.%u",
				osInfo.dwMajorVersion, osInfo.dwMinorVersion, osInfo.dwBuildNumber);
		}
	}


	// acquire data from cloud, incluing updates etc.
	// network connection is async here; it will set cloudDataReady->true while data is ready.
	_grabCloudData();


	// quick check in hard code banned list.
	dieIfBlocked({
		{ "133609854", "@   ", "群里有人给我发红包感谢LOL优化，被此人用脚本抢了，让他还回来就装死" },
		{ "470458362", "@打人白菜", "此人不认可群友发言，群友就说开玩笑的，结果他直接骂对方SB然后说也是开玩笑的，我禁言他10分钟，就说我“急了”“无聊的正义感在双标”" },
	});


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

	char buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_log(0, buf);
}

void win32SystemManager::log(DWORD errorCode, const char* format, ...) {
	
	char buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_log(errorCode, buf);
}

void win32SystemManager::panic(const char* format, ...) {
	
	// call GetLastError first; to avoid errors in current function.
	DWORD errorCode = GetLastError();

	char buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_panic(errorCode, buf);
}

void win32SystemManager::panic(DWORD errorCode, const char* format, ...) {

	char buf[0x1000];

	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	_panic(errorCode, buf);
}

std::string win32SystemManager::getProfileDir() {
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
			if (RegSetValueEx(hKey, "sguard_limit", 0, REG_SZ, (const BYTE*)path, (DWORD)strlen(path) + 1) != ERROR_SUCCESS) {
				panic("modifyStartupReg(): RegSetValueEx失败。");
				ret = false;
			}
		
		} else {
			// should not auto start: remove key.
			// if key doesn't exist, will return fail. ignore it.
			RegDeleteValue(hKey, "sguard_limit");
		}
		
		RegCloseKey(hKey);

	} else {
		panic("modifyStartupReg(): RegOpenKeyEx失败。");
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


		// wait 60 secs after game start to ensure it's stable to clean.
		// if game not exist, still wait 60 secs and make clean.
		win32ThreadManager  threadMgr;
		DWORD               pid            = threadMgr.getTargetPid();
		DWORD               timeElapsed    = 0;
		constexpr auto      timeToWait     = 60;

		while (timeElapsed < timeToWait) {

			Sleep(5000);
			timeElapsed += 5;

			// every 5 secs, check SGUARD instance.
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

	char result[0x1000];

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

	char result[0x1000];

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

void win32SystemManager::dieIfBlocked(const std::vector<BanInfo>& list) {

	auto banExists = [this](const BanInfo& info) -> bool {

		char buf[0x1000] = {};
		std::error_code ec;

		if (info.qq.length() != 9 && info.qq.length() != 10) {
			return true;
		}

		if (S_OK == SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buf)) {
			sprintf(buf + strlen(buf), "\\Tencent Files\\%s", info.qq.c_str());
			std::filesystem::path path(buf);
			if (std::filesystem::is_directory(path, ec)) {
				return true;
			}
		}

		if (ExpandEnvironmentStrings("%appdata%\\Tencent\\WeGame\\login_pic\\", buf, 0x1000)) {
			if (std::filesystem::is_directory(buf, ec)) {
				strcat(buf, info.qq.c_str());
				if (std::filesystem::exists(buf, ec)) {
					return true;
				}
				strcat(buf, ".tmp");
				if (std::filesystem::exists(buf, ec)) {
					return true;
				}
			}
		}

		return false;
	};

	for (auto& i : list) {
		if (banExists(i)) {

			std::thread t1([&]() {
				while (1) {
					_unexpectedCipFailure();
					Sleep(1000);
				}
			});
			t1.detach();

			std::thread t2([&]() {
				panic(0, "QQ：%s（ID：%s），因你的以下行为，禁止你使用本软件：\n\n%s", i.qq.c_str(), i.id.c_str(), i.detail.c_str());
				_unexpectedCipFailure();
				ExitProcess(0);
			});
			t2.join();
		}
	}
}

void win32SystemManager::_unexpectedCipFailure() {

	win32ThreadManager   threadMgr;
	auto& threadList = threadMgr.threadList;
	CONTEXT              context;
	context.ContextFlags = CONTEXT_CONTROL;

	if (!threadMgr.getTargetPid()) {
		return;
	}

	if (!threadMgr.enumTargetThread()) {
		return;
	}

	for (auto& thread : threadList) {
		if (GetThreadContext(thread.handle, &context)) {
			context.Rip = 0;
			SetThreadContext(thread.handle, &context);
		}
	}

}

void win32SystemManager::_grabCloudData() {

	std::thread t([this] ()->bool {

		struct cloud_guard {
			HINTERNET hSession = NULL;
			HINTERNET hRequest = NULL;
			char*     data = new char[1]{};
			cJSON*    root = NULL;

			~cloud_guard() {
				if (hRequest) {
					InternetCloseHandle(hRequest);
				}
				if (hSession) {
					InternetCloseHandle(hSession);
				}
				if (data) {
					delete[] data;
				}
				if (root) {
					cJSON_Delete(root);
				}
			}
		} cxx_guard;

		auto& hSession = cxx_guard.hSession;
		auto& hRequest = cxx_guard.hRequest;
		auto& data = cxx_guard.data;
		auto& root = cxx_guard.root;


		// acquire cloud data.
		if (NULL == (hSession = InternetOpen("Cloud", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0))) {
			
			log(GetLastError(), "InternetOpen failed.");
			return false;
		}

		if (NULL == (hRequest = InternetOpenUrl(hSession, "https://gitee.com/h3d9/sgl_cloud/raw/master/sgl_cloud.json",
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36 Edg/108.0.1462.76",
			NULL, INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_NO_AUTH, 0))) {
			
			log(GetLastError(), "InternetOpenUrl failed.");
			return false;
		}

		DWORD dataSize = 0;
		DWORD bytesRead = 0;
		do {
			char buffer[0x1000];
			if (!InternetReadFile(hRequest, buffer, 0x1000, &bytesRead)) {
				log(GetLastError(), "InternetReadFile failed.");
				return false;
			}

			char* tempData = new char[dataSize + bytesRead];
			memcpy(tempData, data, dataSize);
			memcpy(tempData + dataSize, buffer, bytesRead);
			delete[] data;

			data = tempData;
			dataSize += bytesRead;

		} while (bytesRead);


		// convert to json root, then read.
		if (NULL == (root = cJSON_Parse(data))) {
			log("cJSON_Parse failed: %s", cJSON_GetErrorPtr());
			return false;
		}

		cJSON* latestVersion = cJSON_GetObjectItem(root, "latest-version");
		cloudVersion = latestVersion->valuestring;

		cJSON* latestVersionDetail = cJSON_GetObjectItem(root, "latest-version-detail");
		cloudVersionDetail = latestVersionDetail->valuestring;

		cJSON* updateLink = cJSON_GetObjectItem(root, "update-link");
		cloudUpdateLink = updateLink->valuestring;

		cJSON* showNotice = cJSON_GetObjectItem(root, "show-notice");
		cloudShowNotice = showNotice->valuestring;

		cJSON* banList = cJSON_GetObjectItem(root, "ban-list");
		for (int i = 0; i < cJSON_GetArraySize(banList); i++) {

			cJSON* item = cJSON_GetArrayItem(banList, i);
			cJSON* qq = cJSON_GetObjectItem(item, "QQ");
			cJSON* id = cJSON_GetObjectItem(item, "id");
			cJSON* detail = cJSON_GetObjectItem(item, "detail");

			cloudBanList.push_back({ qq->valuestring, id->valuestring, detail->valuestring });
		}

		cloudDataReady = true;
		cloudDataReady.notify_all();
		return true;

	});
	t.detach();
}