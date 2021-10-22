// 应用层调度器（分时round-robin）
// H3d9于21.2.5，夜。
#include <Windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <Windowsx.h> 
#include <stdio.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <process.h>
#include "limitcore.h"

// dependencies
#include "win32utility.h"

extern volatile bool  g_HijackThreadWaiting;


// Limit Manager
LimitManager  LimitManager::limitManager;

LimitManager::LimitManager() 
	: limitEnabled(true), limitPercent(90) {}

LimitManager& LimitManager::getInstance() {
	return limitManager;
}

void LimitManager::hijack() {

	while (limitEnabled) { // note: 每10+秒重新枚举线程

		win32ThreadManager threadMgr;

		if (threadMgr.getTargetPid() == 0) {
			return; // process is no more alive, exit.
		}

		if (!threadMgr.enumTargetThread()) {
			if (!threadMgr.enumTargetThread(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3FF)) {
				if (!threadMgr.enumTargetThread(THREAD_SUSPEND_RESUME)) {
					return;
				}
			}
		}

		Sleep(300); // forbid busy wait if user stopped limitation.

		// assert: !threadList[elem].Handle.empty && threadList[elem].Handle.valid
		// each loop we manipulate 10+s in target process.
		for (DWORD msElapsed = 0; limitEnabled && msElapsed < 10000;) {

			DWORD TimeRed = limitPercent;
			DWORD TimeGreen = 100 - TimeRed;
			if (limitPercent >= 100) {
				TimeGreen = 1; // 99.9: use 1 slice in 1000
			}

			static bool suspended = false; // false is initialize, not in control flow.

			if (!suspended) {
				for (DWORD i = 0; i < threadMgr.threadCount; i++) {
					if (threadMgr.threadList[i].handle) {
						if (SuspendThread(threadMgr.threadList[i].handle) != (DWORD)-1) {
							suspended = true; // true if at least one of threads is suspended.
						}
					}
				}
			}

			Sleep(TimeRed);
			msElapsed += TimeRed;

			if (suspended) {
				for (DWORD i = 0; i < threadMgr.threadCount; i++) {
					if (threadMgr.threadList[i].handle) {
						if (ResumeThread(threadMgr.threadList[i].handle) != (DWORD)-1) {
							suspended = false;
						}
					}
				}
			}

			Sleep(TimeGreen);
			msElapsed += TimeGreen;
		}

		// release handles (by raii); re-capture them in next loop.
	}

	// user stopped limiting, exit to wait.
}

void LimitManager::enable() {
	limitEnabled = true;
}

void LimitManager::disable() {
	limitEnabled = false;
	while (!g_HijackThreadWaiting); // spin; wait till hijack release target thread.
}

void LimitManager::setPercent(DWORD percent) {
	limitEnabled = true;
	limitPercent = percent;
}