// Ӧ�ò�/�ں�̬����������ʱround-robin��
// H3d9��21.2.5��ҹ��
#include <Windows.h>
#include "limitcore.h"

// dependencies
#include "kdriver.h"
#include "win32utility.h"

extern KernelDriver&          driver;
extern win32SystemManager&    systemMgr;


// Limit Manager
LimitManager  LimitManager::limitManager;

LimitManager::LimitManager() 
	: limitEnabled(true), limitPercent(90), useKernelMode(true) {}

LimitManager& LimitManager::getInstance() {
	return limitManager;
}

void LimitManager::hijack() {

	// entering hijack() (and chase()) we'll set time_critical permanantly,
	// even if user switch to mempatch which don't need that feature.
	// user is expected to get non-time_critical thread at next start-up.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	if (useKernelMode) {

		result_t status;
		
		// check if kernel driver is initialized.
		if (!driver.driverReady) {
			systemMgr.log("hijack(kmode): kdriver is not initialized correctly, quit.");
			return;
		}

		if (!(status = driver.load())) {
			systemMgr.panic(status.error());
			return;
		}

		while (limitEnabled) {

			win32ThreadManager threadMgr;
			auto pid = threadMgr.getTargetPid();

			if (pid == 0) {
				break; // if process is no more alive, exit.
			}


			for (DWORD msElapsed = 0; limitEnabled && msElapsed < 10000;) {

				DWORD TimeRed = limitPercent;
				DWORD TimeGreen = 100 - TimeRed;
				if (limitPercent >= 100) {
					TimeGreen = 1; // 99.9: use 1 slice in 1000
				}

				
				if (!(status = driver.suspend(pid))) {
					const auto& [message, ec] = status.error();
					// nt!pssuspend failed if only proc not exist. in that case, do not log.
					if (ec != 0x5 /* access denied */) {
						systemMgr.log(ec, message);
					}
				}

				Sleep(TimeRed);
				msElapsed += TimeRed;

				if (!(status = driver.resume(pid))) {
					const auto& [message, ec] = status.error();
					if (ec != 0x5 /* access denied */) {
						systemMgr.log(ec, message);
					}
				}

				Sleep(TimeGreen);
				msElapsed += TimeGreen;
			}
		}

		driver.unload();

	} else { // in user-mode

		while (limitEnabled) {

			win32ThreadManager threadMgr;
			auto pid = threadMgr.getTargetPid();

			if (pid == 0) {
				return; // process is no more alive, exit.
			}

			if (!threadMgr.enumTargetThread()) {
				if (!threadMgr.enumTargetThread(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3FF)) {
					if (!threadMgr.enumTargetThread(THREAD_SUSPEND_RESUME)) {
						return;
					}
				}
			}

			// assert: !threadList[elem].Handle.empty && threadList[elem].Handle.valid
			// each loop we manipulate 10+s in target process.
			for (DWORD msElapsed = 0; limitEnabled && msElapsed < 10000;) {

				DWORD TimeRed = limitPercent;
				DWORD TimeGreen = 100 - TimeRed;
				if (limitPercent >= 100) {
					TimeGreen = 1; // 99.9: use 1 slice in 1000
				}

				static bool suspended = false;

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

			// release handles (by raii) if not use Kernel Mode; re-capture them in next loop.
		}
	}

	// user stopped limiting, exit to wait.
}

void LimitManager::enable() {
	limitEnabled = true;
}

void LimitManager::disable() {
	limitEnabled = false;
}

void LimitManager::setPercent(DWORD percent) {
	limitEnabled = true;
	limitPercent = percent;
}