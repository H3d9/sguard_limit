// 线程追踪（手动规则）
// H3d9于2021.7.17，雨。
#include <Windows.h>
#include <tlhelp32.h>
#include "tracecore.h"

// dependencies
#include "win32utility.h"


// Trace module
TraceManager  TraceManager::traceManager;

TraceManager::TraceManager() 
	: lockEnabled(true), lockMode(0), lockRound(95), lockPid(0), lockedThreads{} {}

TraceManager& TraceManager::getInstance() {
	return traceManager;
}

void TraceManager::chase() {   // trace maybe, but i like that word.

	win32ThreadManager     threadMgr;
	DWORD                  pid          = threadMgr.getTargetPid();


	if (pid != 0        /* target exist */ &&
		pid != lockPid  /* target is not current */ ) {

		map threadMap;
		mapIt m1, m2, m3;

		// note: see limit::hijack().
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

		// reset interface.
		for (auto i = 0; i < 3; i++) {
			lockedThreads[i].tid = 0;
			lockedThreads[i].handle = NULL;
			lockedThreads[i].locked = false;
		}

		// wait for preliminary stablize.
		for (auto i = 0; lockEnabled && i < 10; i++) {
			Sleep(1000);
		}

		// identify the biggest metal crusher by sampling 20 secs.
		for (auto time = 0; lockEnabled && time < 20; time++) {

			// each loop we re-scan all threads in target.
			if (!_enumThreadInfo(pid, &threadMap)) {
				break;
			}

			// open threads which are not opened yet.
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (!it->second.handle) {
					it->second.handle = OpenThread(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3FF, FALSE, it->first);
					if (!it->second.handle) {
						it->second.handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, it->first);
					}
				}
			}

			// calc thread's instruction period time delta.
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				ULONG64 cycles;
				QueryThreadCycleTime(it->second.handle, &cycles);
				it->second.cycleDelta = cycles - it->second.cycles;
				it->second.cycles = cycles;
			}

			// die rule: cycles >= 1e7, or not but used to.
			// survive rule: cycles <= 8e6.
			m1 = threadMap.begin();
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (it->second.cycleDelta > m1->second.cycleDelta) {
					m1 = it;
				}
			}
			if (m1->second.cycleDelta >= 20000000) {
				m1->second.dieCount += 1;
			}

			Sleep(1000);
		}

		if (lockEnabled && !threadMap.empty() &&
			(m1->second.cycleDelta >= 30000000 || m1->second.dieCount >= 5)) {
			lockedThreads[0].tid = m1->first;
			lockedThreads[0].handle = m1->second.handle;
		}

		// wait till system is stable.
		for (auto i = 0; lockEnabled && i < 10; i++) {
			Sleep(1000);
		}

		// prepare to determine other threads. 
		for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
			it->second.dieCount = 0;
		}

		// sample 15 secs. 
		// we use the same methods as above.
		for (auto time = 0; lockEnabled && time < 15; time++) {

			if (!_enumThreadInfo(pid, &threadMap)) {
				break;
			}

			// diff: remove thread which have been identified before.
			if (lockedThreads[0].tid) {
				threadMap.erase(lockedThreads[0].tid.load());
			}

			// target have less than 3 thread, break out.
			if (threadMap.size() <= 1) {
				break;
			}

			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (!it->second.handle) {
					it->second.handle = OpenThread(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3FF, FALSE, it->first);
					if (!it->second.handle) {
						it->second.handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, it->first);
					}
				}
			}

			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				ULONG64 cycles;
				QueryThreadCycleTime(it->second.handle, &cycles);
				it->second.cycleDelta = cycles - it->second.cycles;
				it->second.cycles = cycles;
			}

			// find top 2, which have maximum cycles.
			m2 = threadMap.begin();
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (it->second.cycleDelta > m2->second.cycleDelta) {
					m2 = it;
				}
			}
			if (m2->second.cycleDelta >= 10000000) {
				m2->second.dieCount += 1;
			}

			ULONG64 m2cycleDelta = m2->second.cycleDelta;
			m2->second.cycleDelta = 0;

			m3 = threadMap.begin();
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (it->second.cycleDelta > m3->second.cycleDelta) {
					m3 = it;
				}
			}
			if (m3->second.cycleDelta >= 10000000) {
				m3->second.dieCount += 1;
			}

			m2->second.cycleDelta = m2cycleDelta;

			Sleep(1000);
		}

		if (!(threadMap.size() <= 1)) {
			if (lockEnabled && !threadMap.empty() && m2->second.dieCount >= 7) {
				lockedThreads[1].tid = m2->first;
				lockedThreads[1].handle = m2->second.handle;
			}
			if (lockEnabled && !threadMap.empty() && m3->second.dieCount >= 7) {
				lockedThreads[2].tid = m3->first;
				lockedThreads[2].handle = m3->second.handle;
			}
		}

		// release useless handles.
		for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
			if (it->first != 0) {
				if (it->first != lockedThreads[0].tid &&
					it->first != lockedThreads[1].tid &&
					it->first != lockedThreads[2].tid) {
					CloseHandle(it->second.handle);
				}
			}
		}

		if (lockEnabled && !threadMap.empty()) {
			lockPid = pid;
		} else {
			return;
		}
	}


	// userwait: exit if process is killed or user interactive.
	while (lockEnabled) {

		pid = threadMgr.getTargetPid();

		if (pid == 0 /* target no more exists */ || pid != lockPid /* target is not current */) {
			lockPid = 0;  
			break;
		}

		// assert: 1 <= lockRound <= 99
		DWORD unlockRound = 100 - lockRound;

		// fallback: process is not restarted && retrieved thread handles
		switch (lockMode) {
			case 0:  // lock 3
			{
				for (auto ms = 0; lockEnabled && ms < 5000; ms += 100) {
					for (auto i = 0; i < 3; i++) {
						if (lockedThreads[i].handle && !lockedThreads[i].locked) {
							SuspendThread(lockedThreads[i].handle);
							lockedThreads[i].locked = true;
						}
					}
					Sleep(100);
				}
			}
			break;
			case 1:  // lock 3 rr
			{
				for (auto ms = 0; lockEnabled && ms < 5000; ms += 100) {
					for (auto i = 0; i < 3; i++) {
						if (lockedThreads[i].handle && !lockedThreads[i].locked) {
							SuspendThread(lockedThreads[i].handle);
							lockedThreads[i].locked = true;
						}
					}
					Sleep(lockRound);
					for (auto i = 0; i < 3; i++) {
						if (lockedThreads[i].handle && lockedThreads[i].locked) {
							ResumeThread(lockedThreads[i].handle);
							lockedThreads[i].locked = false;
						}
					}
					Sleep(unlockRound);
				}
			}
			break;
			case 2:  // lock 1
			{
				for (auto ms = 0; lockEnabled && ms < 5000; ms += 100) {
					if (lockedThreads[0].handle && !lockedThreads[0].locked) {
						SuspendThread(lockedThreads[0].handle);
						lockedThreads[0].locked = true;
					}
					if (lockedThreads[1].handle && lockedThreads[1].locked) {
						ResumeThread(lockedThreads[1].handle);
						lockedThreads[1].locked = false;
					}
					if (lockedThreads[2].handle && lockedThreads[2].locked) {
						ResumeThread(lockedThreads[2].handle);
						lockedThreads[2].locked = false;
					}
					Sleep(100);
				}
			}
			break;
			case 3:  // lock 1 rr
			{
				if (lockedThreads[1].handle && lockedThreads[1].locked) {
					ResumeThread(lockedThreads[1].handle);
					lockedThreads[1].locked = false;
				}
				if (lockedThreads[2].handle && lockedThreads[2].locked) {
					ResumeThread(lockedThreads[2].handle);
					lockedThreads[2].locked = false;
				}
				for (auto ms = 0; lockEnabled && ms < 5000; ms += 100) {
					if (lockedThreads[0].handle && !lockedThreads[0].locked) {
						SuspendThread(lockedThreads[0].handle);
						lockedThreads[0].locked = true;
					}
					Sleep(lockRound);
					if (lockedThreads[0].handle && lockedThreads[0].locked) {
						ResumeThread(lockedThreads[0].handle);
						lockedThreads[0].locked = false;
					}
					Sleep(unlockRound);
				}
			}
			break;
		}
	}
}

void TraceManager::enable() {
	lockEnabled = true;
}

void TraceManager::disable() {
	lockEnabled = false;
	for (auto i = 0; i < 3; i++) {
		if (lockedThreads[i].locked) {
			ResumeThread(lockedThreads[i].handle);
			lockedThreads[i].locked = false;
		}
	}
}

void TraceManager::setMode(DWORD mode) {
	if (mode >= 0 && mode <= 3) {
		lockEnabled = true;
		lockMode = mode;
	}
}

bool TraceManager::_enumThreadInfo(DWORD pid, map* m) {

	THREADENTRY32 te;
	te.dwSize = sizeof(THREADENTRY32);

	DWORD currentTids[512], tidCount = 0;
	ZeroMemory(currentTids, sizeof(currentTids));

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

	// add new threads to map. On (n<=1e5)
	for (BOOL next = Thread32First(hSnapshot, &te); next; next = Thread32Next(hSnapshot, &te)) {
		if (te.th32OwnerProcessID == pid) {
			DWORD tid = currentTids[tidCount++] = te.th32ThreadID;
			if (m->count(tid) == 0) {
				(*m) [tid] = threadinfo();
			}
		}
	}

	// remove expired threads. On^2 (n<=20)
	for (auto it = m->begin(); it != m->end();) {
		bool found = false;
		for (DWORD i = 0; i < tidCount; i++) {
			if (it->first == currentTids[i]) {
				found = true;
				break;
			}
		}
		if (found) ++it;
		else it = m->erase(it);
	}

	CloseHandle(hSnapshot);

	return !m->empty();
}