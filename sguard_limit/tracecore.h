#pragma once
#include <Windows.h>
#include <unordered_map>

// wndproc button command
#define IDM_LOCK3           208
#define IDM_LOCK1           209
#define IDM_LOCK3RR         210
#define IDM_LOCK1RR         211
#define IDM_SETRRTIME       212
#define IDM_UNLOCK          213


// Trace module (sington)
class TraceManager {

private:
	static TraceManager         traceManager;

private:
	TraceManager();
	~TraceManager()                                  = default;
	TraceManager(const TraceManager&)                = delete;
	TraceManager(TraceManager&&)                     = delete;
	TraceManager& operator= (const TraceManager&)    = delete;
	TraceManager& operator= (TraceManager&&)         = delete;

public:
	static TraceManager&        getInstance();

public:
	volatile bool               lockEnabled;
	volatile DWORD              lockMode;
	volatile DWORD              lockRound;

public:
	void      chase();
	void      enable();
	void      disable();
	void      setMode(DWORD mode);
	void      wndProcAddMenu(HMENU hMenu);

private:
	struct lockedThreads_t {
		DWORD         tid             = 0;
		HANDLE        handle          = NULL;   // handle == NULL : not locked.
		bool          locked          = false;
	};

	struct threadinfo {
		HANDLE        handle          = NULL;
		ULONG64       cycles          = 0;
		ULONG64       cycleDelta      = 0;
		int           dieCount        = 0;
	};

	using  map    = std::unordered_map<DWORD, threadinfo>;  // hashmap: tid -> {...}
	using  mapIt  = decltype(map().begin());

	DWORD                       lockPid;
	lockedThreads_t             lockedThreads[3];

private:
	bool      _enumThreadInfo(DWORD pid, map* m);
};