// x64 SGUARD限制器，适用于各种腾讯游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include <thread>
#include <mutex>
#include "resource.h"
#include "wndproc.h"
#include "win32utility.h"
#include "config.h"
#include "kdriver.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"


KernelDriver&           driver                  = KernelDriver::getInstance();
win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
ConfigManager&          configMgr               = ConfigManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
TraceManager&           traceMgr                = TraceManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();

volatile bool           g_HijackThreadWaiting   = true;
volatile DWORD          g_Mode                  = 2;      // 0: lim  1: lock  2: patch

volatile bool           g_KillAceLoader         = true;


static void CleanThreadWorker() {

	// this clean thread cannnot be re-entered, so there we have a lock.
	// [note] former call will give up if game has re-launched, which is identified by pid.
	static std::mutex mtx;

	mtx.lock();
	systemMgr.log("ace-killer thread: entering critical section.");

	win32ThreadManager  threadMgr;
	DWORD               pid            = threadMgr.getTargetPid();
	DWORD               timeElapsed    = 0;
	constexpr auto      timeToWait     = 60;

	if (pid) {

		// ensure SG's pid not changed before we eliminate ace-loader,
		// that's because pid change identifys game re-launch.
		systemMgr.log("ace-killer thread: 1 min wait begin.");

		do {
			Sleep(5000);
			timeElapsed += 5;
		} while ( timeElapsed < timeToWait && pid == threadMgr.getTargetPid() );


		// if wait success, try kill ace-loader.
		// there maybe multiple ace-loader(s), so do a while.
		// [note] check pid finally, that's to ensure kill is immediately after check.
		// check pid won't execute twice at one time, no matter wait success or fail.
		if (timeElapsed >= timeToWait && pid == threadMgr.getTargetPid()) {

			while ( threadMgr.getTargetPid("GameLoader.exe") ) {

				if (threadMgr.killTarget()) {
					systemMgr.log("ace-killer thread: eliminated ace-loader, pid: %u", threadMgr.pid);

				} else {
					systemMgr.log(GetLastError(), "ace-killer thread: failed to kill target.");
					break;
				}
			}

		} else {
			systemMgr.log("ace-killer thread: abort waiting: game re-launched.");
		}

	}

	systemMgr.log("ace-killer thread: leaving critical section.");
	mtx.unlock();
}

static void HijackThreadWorker() {
	
	systemMgr.log("hijack thread: created.");

	win32ThreadManager threadMgr;

	while (1) {

		// scan per 5 seconds when idle; 
		// if process is found, trap into usr-selected mode.
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");

			// raise ace-killer thread if switch is enabled.
			if (g_KillAceLoader) {

				auto CleanThreadCaller = []() {
					std::thread cleanThread(CleanThreadWorker);
					cleanThread.detach();
				};

				CleanThreadCaller();
			}

			// select mode.
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				g_HijackThreadWaiting = false;
				limitMgr.hijack();
				g_HijackThreadWaiting = true;
			}
			if (g_Mode == 1 && traceMgr.lockEnabled) {
				g_HijackThreadWaiting = false;
				traceMgr.chase();
				g_HijackThreadWaiting = true;
			}
			if (g_Mode == 2 && patchMgr.patchEnabled) {
				g_HijackThreadWaiting = false;
				patchMgr.patch();
				g_HijackThreadWaiting = true;
			}
		}

		Sleep(5000); // call sys schedule | no target found, wait.
		// note: volatiles are written by single thread and read by multi thread. no need to sync.
	}
}


INT WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd) {

	bool status;


	// initialize system module: 
	// setup dpi and raise privilege (must do first)
	// init system manager (check sington, get path and os version, init log subsystem)
	// init win32 gui (create window for callback, create tray)

	systemMgr.setupProcessDpi();

	systemMgr.enableDebugPrivilege();

	status =
	systemMgr.checkDebugPrivilege();

	if (!status) {
		return -1;
	}

	status =
	systemMgr.systemInit(hInstance);

	if (!status) {
		return -1;
	}

	status =
	systemMgr.createWindow(WndProc, IDI_ICON1);
	
	if (!status) {
		return -1;
	}

	systemMgr.createTray(WM_TRAYACTIVATE);


	// initialize configuration module:
	// load all user options from given path.
	// after load, if modules needs to modify some config, just change and writeconfig().

	configMgr.init(systemMgr.getProfileDir());

	status =
	configMgr.loadConfig();

	if (!status) {
		MessageBox(0,
			"【更新说明】\n\n"
			" 内存补丁 V4：新增“高级内存搜索”（当前支持到Win11.22512）。\n\n"
			"1. 新增功能：启动游戏60秒后，结束ace-loader。\n\n\n"
			
			"【重要提示】\n\n"
			"1. 本工具是免费软件，任何出售本工具的人都是骗子哦！\n\n"
			"2. 默认模式为内存补丁。LOL可以换“时间转轮”，但DNF不建议换。\n"
			"3. “高级内存搜索”默认开游戏20秒后开始执行。\n\n"
			"4. 若你第一次使用，请【务必】仔细阅读说明（右键菜单→其他选项）。",
			VERSION "  by: @H3d9", MB_OK);
	}


	// initialize kdriver module:
	// make preparations for load kernel driver from given path.
	// however, if (driver init fails && user selected related options), modify config and panic.

	if (systemMgr.getSystemVersion() == OSVersion::OTHERS) {
		systemMgr.panic(0, "内核驱动模块在你的操作系统上不受支持。\n"
			               "【注】内核驱动仅支持win7/10/11系统。");
	} else {

		status =
		driver.init(systemMgr.getProfileDir());

		if (!status && (g_Mode == 2 || (g_Mode == 0 && limitMgr.useKernelMode))) {

			// turn off related config flags.
			limitMgr.useKernelMode = false;
			configMgr.writeConfig();

			// show panic: alert usr to switch options manually.
			systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
			systemMgr.panic(0, "由于驱动初始化失败，以下关联模块无法使用：\n\n"
							   "内存补丁 V4\n"
							   "内核态调度器\n");
		}
	}


	// create working thread:
	// using std::thread (_beginthreadex) is more safe than winapi CreateThread;
	// because we use heap and crt functions in working thread.

	auto HijackThreadCaller = [] () {
		std::thread hijackThread(HijackThreadWorker);
		hijackThread.detach();
	};

	HijackThreadCaller();


	// enter primary msg loop:
	// main thread will wait for window msgs from usr, while working thread do actual works.

	auto result =
	systemMgr.messageLoop();


	// program exit:
	// after winmain() returns, all static sington objects are destructed.

	systemMgr.removeTray();

	return (INT) result;
}