// x64 SGUARD限制器，适用于各种腾讯游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include "win32utility.h"
#include "wndproc.h"
#include "panic.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"

win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
TraceManager&           traceMgr                = TraceManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();

volatile bool           g_bHijackThreadWaiting  = true;
volatile DWORD          g_Mode                  = 2;      // 0: lim  1: lock  2: patch


static DWORD WINAPI HijackThreadWorker(LPVOID) {
	
	win32ThreadManager threadMgr;

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	
	while (1) {

		systemMgr.log("hijack thread: check pid.");

		// scan per 5 seconds when idle; if process is found, trap into hijack()。
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				g_bHijackThreadWaiting = false;   // sync is done as we call schedule
				limitMgr.hijack();                // start hijack.
				g_bHijackThreadWaiting = true;
			}
			if (g_Mode == 1 && traceMgr.lockEnabled) {
				g_bHijackThreadWaiting = false;
				traceMgr.chase();
				g_bHijackThreadWaiting = true;
			}
			if (g_Mode == 2 && patchMgr.patchEnabled) {
				g_bHijackThreadWaiting = false;
				systemMgr.log("hijack thread: entering patchMgr::patch().");
				patchMgr.patch();
				systemMgr.log("hijack thread: leaving patchMgr::patch().");
				g_bHijackThreadWaiting = true;
			}
		}

		systemMgr.log("hijack thread: waiting.");
		Sleep(5000); // call sys schedule | no target found, wait.
	}
}

INT WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd) {

	bool      status;


	systemMgr.setupProcessDpi();

	systemMgr.systemInit(hInstance);

	status = 
	systemMgr.createWin32Window(WndProc);
	
	if (!status) {
		panic("创建窗口失败。");
		return -1;
	}

	systemMgr.enableDebugPrivilege();

	status = 
	systemMgr.checkDebugPrivilege();
	
	if (!status) {
		panic("提升权限失败，请右键管理员运行。");
		return -1;
	}

	systemMgr.createTray();

	status = 
	systemMgr.loadConfig();

	if (!status) {
		MessageBox(0,
			"首次使用说明：\n\n"
			"更新模式：MemPatch V2\n\n"
			"增加稳定性，修复旧版导致SG卡掉的问题。\n"
			"增加控制选项，以适应不同机器。\n"
			"\n\n"
			"【提示】双击右下角托盘图标，可以查看新版详细说明。",
			VERSION " colg@H3d9", MB_OK);
		ShellExecute(0, "open", "https://bbs.colg.cn/thread-8305966-1-1.html", 0, 0, SW_HIDE);
	}

	patchMgr.patchInit();


	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	HANDLE hijackThread = 
	CreateThread(NULL, NULL, HijackThreadWorker, NULL, 0, 0);
	
	if (!hijackThread) {
		panic("创建工作线程失败。");
		return -1;
	}

	CloseHandle(hijackThread);


	auto result = 
	systemMgr.messageLoop();

	systemMgr.removeTray();

	return (INT) result;
}