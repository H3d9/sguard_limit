#pragma once
#include <Windows.h>
#include <unordered_map>


// Trace module (sington)
class TraceManager {

private:
	static TraceManager    traceManager;

private:
	TraceManager();
	~TraceManager()                                  = default;
	TraceManager(const TraceManager&)                = delete;
	TraceManager(TraceManager&&)                     = delete;
	TraceManager& operator= (const TraceManager&)    = delete;
	TraceManager& operator= (TraceManager&&)         = delete;

public:
	static TraceManager&   getInstance();

public:
	void      chase();
	void      enable();
	void      disable();
	void      setMode(DWORD mode);

public:
	struct lockedThreads_t {
		DWORD     tid        = 0;
		HANDLE    handle     = NULL;   // handle == NULL : not locked.
		bool      locked     = false;
	};

	volatile bool               lockEnabled;
	volatile DWORD              lockMode;
	volatile DWORD              lockRound;
	volatile DWORD              lockPid;
	volatile lockedThreads_t    lockedThreads[3];

private:
	struct threadinfo {
		HANDLE        handle          = NULL;
		ULONG64       cycles          = 0;
		ULONG64       cycleDelta      = 0;
		int           dieCount        = 0;
	};

	using map   = std::unordered_map<DWORD, threadinfo>;  // hashmap: tid -> {...}
	using mapIt = decltype(map().begin());

private:
	bool      _enumThreadInfo(DWORD pid, map* m);
};