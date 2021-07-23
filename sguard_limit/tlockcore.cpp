// 规则针对性线程锁
// H3d9于2021.7.17，雨。
#include <Windows.h>
#include <tlhelp32.h>
#include <process.h>
#include <unordered_map>

#include "limitcore.h"  // GetProcessID
#include "tlockcore.h"


volatile bool lockEnabled = true;
volatile lockedThreads_t lockedThreads[3];
volatile DWORD lockPid = 0;


struct threadinfo {
	HANDLE    handle = 0;
	ULONG64   cycles = 0;
	ULONG64   cycleDelta = 0;
	int       dieCount = 0;
};

using map = std::unordered_map<DWORD, threadinfo>;  // tid -> {...}
using mapIt = decltype(map().begin());


static bool EnumThreadInfo(DWORD pid, map* m) {
	/* process threads info->map. */

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

void threadLock(DWORD pid) {

	if (lockPid != pid) {

		map threadMap;
		mapIt m1, m2, m3;

		// reset interface.
		for (auto i = 0; i < 3; i++) {
			lockedThreads[i].tid = 0;
			lockedThreads[i].handle = NULL;
			lockedThreads[i].locked = false;
		}

		// wait for preliminary stablize.
		Sleep(10000);

		// identify the biggest metal crusher by sampling 30 secs.
		for (auto time = 0; lockEnabled && time < 30; time++) {

			// each loop we re-scan all threads in target.
			if (!EnumThreadInfo(pid, &threadMap)) {
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
			SuspendThread(m1->second.handle);
			lockedThreads[0].tid = m1->first;
			lockedThreads[0].handle = m1->second.handle;
			lockedThreads[0].locked = true;
		}

		// wait till system is stable.
		for (auto i = 0; lockEnabled && !threadMap.empty() && i < 30; i++) {
			Sleep(1000);
		}

		// prepare to lock other threads. 
		for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
			it->second.dieCount = 0;
		}

		// sample 15 secs. 
		// we use the same methods as above.
		for (auto time = 0; lockEnabled && time < 15; time++) {

			if (!EnumThreadInfo(pid, &threadMap)) {
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

			// find top 2 which have maximum cycles.
			m2 = threadMap.begin();
			for (auto it = threadMap.begin(); it != threadMap.end(); ++it) {
				if (it->second.cycleDelta > m2->second.cycleDelta) {
					m2 = it;
				}
			}
			if (m2->second.cycleDelta >= 10000000) {
				m2->second.dieCount += 1;
			}

			ULONG64 maxCycles = m2->second.cycleDelta;
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

			m2->second.cycleDelta = maxCycles;

			Sleep(1000);
		}

		if (lockEnabled && !threadMap.empty() && m2->second.dieCount >= 7) {
			SuspendThread(m2->second.handle);
			lockedThreads[1].tid = m2->first;
			lockedThreads[1].handle = m2->second.handle;
			lockedThreads[1].locked = true;
		}
		if (lockEnabled && !threadMap.empty() && m3->second.dieCount >= 7) {
			SuspendThread(m3->second.handle);
			lockedThreads[2].tid = m3->first;
			lockedThreads[2].handle = m3->second.handle;
			lockedThreads[2].locked = true;
		}

		if (lockEnabled && !threadMap.empty()) {
			lockPid = pid;
		} else {
			return;
		}
	}


	// userwait: exit if process is killed or user interactive.
	while (lockEnabled) {
		if (lockPid != GetProcessID("SGuard64.exe")) {
			lockPid = 0;
			break;
		}
		// fallback: process is not restarted && retrieved thread handles
		for (auto i = 0; lockEnabled && i < 3; i++) {
			if (lockedThreads[i].handle && !lockedThreads[i].locked) {
				SuspendThread(lockedThreads[i].handle);
				lockedThreads[i].locked = true;
			}
		}
		Sleep(5000);
	}
}