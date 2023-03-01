#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <tuple>
#include <tl/expected.hpp> // c++23 p0323r3: not implemented in msvc 14.2
#include <fmt/core.h> // msvc std::format generates large file size

using error_t  = std::tuple<std::string, DWORD>;
using result_t = tl::expected<bool, error_t>;

using fmt::format;


// system version (kdriver support)
enum class OSVersion { 
	WIN_7       = 1, 
	WIN_8,
	WIN_81,
	WIN_10_11,
	OTHERS
};


// thread module wrapper (raii, see: std::shared_ptr)
struct win32Thread {

	// general properties
	DWORD       tid;
	HANDLE      handle;

	// time properties
	ULONG64     cycles;
	ULONG64     cycleDelta;
	ULONG64     cycleDeltaAvg;

	// ctors
	win32Thread(DWORD tid, DWORD desiredAccess = THREAD_ALL_ACCESS);
	~win32Thread();
	win32Thread(const win32Thread& t);
	win32Thread(win32Thread&& t) noexcept;
	win32Thread& operator= (win32Thread t) noexcept;
	// win32Thread& operator= (win32Thread&&);  >> no need; overload resolution is before '=delete'
	
private:
	DWORD*      _refCount;

private:
	static void _mySwap(win32Thread& t1, win32Thread& t2);  // friend ADL? no way, i don't need inline
};


// general thread toolkit
class win32ThreadManager {

public:
	win32ThreadManager();
	~win32ThreadManager()                                      = default;
	win32ThreadManager(const win32ThreadManager&)              = delete;
	win32ThreadManager(win32ThreadManager&&)                   = delete;
	win32ThreadManager& operator= (const win32ThreadManager&)  = delete;
	win32ThreadManager& operator= (win32ThreadManager&&)       = delete;

	DWORD  getTargetPid(const char* procName = "SGuard64.exe");
	bool   killTarget();
	bool   enumTargetThread(DWORD desiredAccess = THREAD_ALL_ACCESS);

public:
	DWORD                      pid;
	DWORD                      threadCount;
	std::vector<win32Thread>   threadList;
};


// general system toolkit (sington)
class win32SystemManager {

private:
	static win32SystemManager  systemManager;

private:
	win32SystemManager();
	~win32SystemManager();
	win32SystemManager(const win32SystemManager&)               = delete;
	win32SystemManager(win32SystemManager&&)                    = delete;
	win32SystemManager& operator= (const win32SystemManager&)   = delete;
	win32SystemManager& operator= (win32SystemManager&&)        = delete;

public:
	static win32SystemManager&  getInstance();

public:
	bool       systemInit(HINSTANCE hInstance);
	bool       runWithUac();
	void       setupProcessDpi();
	bool       enableDebugPrivilege();
	bool       createWindow(WNDPROC WndProc, DWORD WndIcon);
	void       createTray(UINT trayActiveMsg);
	void       removeTray();
	WPARAM     messageLoop();

public:
	void       log(std::string logMessage);
	void       log(error_t unexpectedObject);
	void       log(DWORD errorCode, std::string logMessage);

	void       panic(std::string errorMessage);
	void       panic(error_t unexpectedObject);
	void       panic(DWORD errorCode, std::string errorMessage);

public:
	std::string   getProfileDir();     // xref: config, kdriver
	OSVersion     getSystemVersion();  // xref: mempatch
	DWORD         getSystemBuildNum(); // xref: mempatch

public:
	bool       modifyStartupReg();  // add/remove registry based on autoStartup
	void       raiseCleanThread();  // clean GameLoader as game started


public:
	struct BanInfo {
		using str = std::string;
		str qq, id, detail;
		BanInfo(str qq, str id, str detail) : qq(qq), id(id), detail(detail) {}
	};

	std::atomic<bool>      cloudDataReady;
	std::string            cloudVersion;
	std::string            cloudVersionDetail;
	std::string            cloudUpdateLink;
	std::string            cloudShowNotice;
	std::vector<BanInfo>   cloudBanList;

	void       dieIfBlocked(const std::vector<BanInfo>& list);

private:
	void       _grabCloudData();
	void       _unexpectedCipFailure();


public:
	bool                   autoStartup;
	bool                   autoCheckUpdate;
	bool                   killAceLoader;

public:
	HINSTANCE              hInstance;
	HWND                   hWnd;

private:
	HANDLE                 hProgram;

	std::string            profileDir;
	OSVersion              osVersion;
	DWORD                  osBuildNum;

	FILE*                  logfp;
	NOTIFYICONDATA         icon;
};