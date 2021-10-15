#pragma once
#include <Windows.h>
#include <vector>

// system version (used in patch module)
enum class OSVersion { 
	WIN_7   = 1, 
	WIN_10,
	OTHERS
};


// thread module wrapper (raii, see: std::shared_ptr)
struct win32Thread {

	// general properties
	DWORD       tid;
	HANDLE      handle;

	// module properties
	ULONG64     cycles;
	ULONG64     cycleDelta;
	ULONG64     rip; 

	// ctors
	win32Thread(DWORD tid = 0, DWORD desiredAccess = THREAD_ALL_ACCESS);
	~win32Thread();
	win32Thread(const win32Thread& t);
	win32Thread(win32Thread&& t) noexcept;
	win32Thread& operator= (const win32Thread& t);
	win32Thread& operator= (win32Thread&& t) noexcept;

private:
	// do not use mutable data, which won't SHARE reference.
	DWORD*      _refCount;

private:
	static void _mySwap(win32Thread& t1, win32Thread& t2);
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

	DWORD  getTargetPid();
	bool   enumTargetThread(DWORD desiredAccess = THREAD_ALL_ACCESS);

public:
	DWORD                        pid;
	DWORD                        threadCount;
	std::vector<win32Thread>     threadList;
};


// general system toolkit (sington)
class win32SystemManager {

private:
	static win32SystemManager    systemManager;

private:
	win32SystemManager();
	~win32SystemManager();
	win32SystemManager(const win32SystemManager&)               = delete;
	win32SystemManager(win32SystemManager&&)                    = delete;
	win32SystemManager& operator= (const win32SystemManager&)   = delete;
	win32SystemManager& operator= (win32SystemManager&&)        = delete;

public:
	static win32SystemManager&   getInstance();


public:
	static constexpr DWORD       WM_TRAYACTIVATE = WM_APP + 10U;

	HWND                         hWnd;
	HINSTANCE                    hInstance;

public:
	bool        systemInit(HINSTANCE hInst);
	void        setupProcessDpi();
	void        enableDebugPrivilege();
	bool        checkDebugPrivilege();
	bool        createWin32Window(WNDPROC WndProc);
	void        createTray();
	void        removeTray();
	WPARAM      messageLoop();
			    
	bool        loadConfig();
	void        writeConfig();

	void        log(const char* format, ...);
			    
public:
	CHAR*       sysfilePath();
	OSVersion   getSystemVersion();

private:
	ATOM        _registerMyClass(WNDPROC WndProc);

private:
	HANDLE                       hProgram;
	OSVersion                    osVersion;
	FILE*                        logfp;
	NOTIFYICONDATA               icon;
	CHAR                         profileDir[1024];
	CHAR                         profile[1024];
	CHAR                         sysfile[1024];
	CHAR                         logfile[1024];
};