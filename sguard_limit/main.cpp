// x64 SGUARD限制器，适用于各种腾讯游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include <thread>
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


static void HijackThreadWorker() {
	
	systemMgr.log("hijack thread: created.");

	win32ThreadManager threadMgr;

	while (1) {

		// scan per 5 seconds when idle; 
		// if process is found, trap into usr-selected mode.
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");

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
			"【首次使用说明】\n\n"
			"1 修订细节以增加稳定性。\n"
			"2 修改部分说明。\n\n\n"
			"更新模式：MemPatch V3\n\n"
			"【新特性】在上一版的基础上进一步压缩SGUARD的cpu使用率令其接近0。\n"
			"（但必要时SGUARD仍会短暂占用cpu以防游戏出现异常）\n\n\n"
			"【重要提示】若你第一次使用，建议仔细阅读说明（右键菜单->其他选项）。\n",
			VERSION "  by: @H3d9", MB_OK);
	}


	// initialize kdriver module:
	// make preparations for load kernel driver from given path.
	// however, if (driver init fails && user selected related options), modify config and panic.

	status =
	driver.init(systemMgr.getProfileDir());

	if (!status && (g_Mode == 2 || (g_Mode == 0 && limitMgr.useKernelMode))) {

		// turn off related config flags.
		limitMgr.useKernelMode = false;
		configMgr.writeConfig();

		// show panic: alert usr to switch options manually.
		systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
		systemMgr.panic(0, "由于驱动初始化失败，以下关联模块无法使用：\n\n"
			               "MemPatch V3\n"
			               "内核态调度器\n");
	}


	// create working thread:
	// using std::thread (_beginthreadex) is more safe than winapi CreateThread;
	// because we use crt functions in working thread.

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