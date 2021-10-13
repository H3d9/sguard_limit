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
#include "win32utility.h"
#include "wndproc.h"
#include "panic.h"

#include "limitcore.h"

extern volatile bool  g_bHijackThreadWaiting;


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
	while (!g_bHijackThreadWaiting); // spin; wait till hijack release target thread.
}

void LimitManager::setPercent(DWORD percent) {
	limitEnabled = true;
	limitPercent = percent;
}

void LimitManager::wndProcAddMenu(HMENU hMenu) {

	if (!limitEnabled) {
		AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
	} else if (g_bHijackThreadWaiting) {
		AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
	} else {
		AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 侦测到SGuard");
	}
	AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：时间片轮转 [点击切换]");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MFT_STRING, IDM_PERCENT90, "限制资源：90%");
	AppendMenu(hMenu, MFT_STRING, IDM_PERCENT95, "限制资源：95%");
	AppendMenu(hMenu, MFT_STRING, IDM_PERCENT99, "限制资源：99%");
	AppendMenu(hMenu, MFT_STRING, IDM_PERCENT999, "限制资源：99.9%");
	AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT, "停止限制");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
	if (limitEnabled) {
		switch (limitPercent) {
		case 90:
			CheckMenuItem(hMenu, IDM_PERCENT90, MF_CHECKED);
			break;
		case 95:
			CheckMenuItem(hMenu, IDM_PERCENT95, MF_CHECKED);
			break;
		case 99:
			CheckMenuItem(hMenu, IDM_PERCENT99, MF_CHECKED);
			break;
		case 999:
			CheckMenuItem(hMenu, IDM_PERCENT999, MF_CHECKED);
			break;
		}
	} else {
		CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_CHECKED);
	}
}