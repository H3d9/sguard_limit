// x64 SGUARD限制器，适用于各种腾讯游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include "wndproc.h"
#include "resource.h"
#include "win32utility.h"
#include "config.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"

win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
ConfigManager&          configMgr               = ConfigManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
TraceManager&           traceMgr                = TraceManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();

volatile bool           g_HijackThreadWaiting   = true;
volatile DWORD          g_Mode                  = 2;      // 0: lim  1: lock  2: patch


static DWORD WINAPI HijackThreadWorker(LPVOID) {
	
	win32ThreadManager threadMgr;

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	
	systemMgr.log("hijack thread: created.");

	while (1) {

		// scan per 5 seconds when idle; if process is found, trap into hijack()。
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				g_HijackThreadWaiting = false;   // sync is done as we call schedule
				limitMgr.hijack();                // start hijack.
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
	}
}

INT WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd) {

	bool status;


	systemMgr.setupProcessDpi();

	status =
	systemMgr.init(hInstance, IDI_ICON1, WM_TRAYACTIVATE);

	if (!status) {
		return -1;
	}

	systemMgr.enableDebugPrivilege();

	status =
	systemMgr.checkDebugPrivilege();

	if (!status) {
		return -1;
	}

	configMgr.init(systemMgr.profilePath());

	patchMgr.init();

	status = 
	systemMgr.createWin32Window(WndProc);
	
	if (!status) {
		return -1;
	}

	systemMgr.createTray();

	status = 
	configMgr.loadConfig();

	if (!status) {
		MessageBox(0,
			"首次使用说明：\n\n"
			"更新模式：MemPatch V2\n\n"
			"1 修复旧版（21.10.17）在【win7】下无效的问题。\n\n"
			"2 修复旧版（21.10.16）在【win11】下无效的问题。\n\n"
			"  (特别感谢@白嫖怪 提供的远程win11系统)\n\n"
			"【注意】默认关闭增强模式（NtWaitForSingleObject）。开这个之前，最好做好游戏异常的准备。\n\n\n"
			"【提示】双击右下角托盘图标，可以查看新版详细说明。",
			VERSION " colg@H3d9", MB_OK);
		ShellExecute(0, "open", "https://bbs.colg.cn/thread-8305966-1-1.html", 0, 0, SW_HIDE);
	}


	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	HANDLE hijackThread = 
	CreateThread(NULL, NULL, HijackThreadWorker, NULL, 0, 0);
	
	if (!hijackThread) {
		systemMgr.panic("创建工作线程失败。");
		return -1;
	}

	CloseHandle(hijackThread);


	auto result = 
	systemMgr.messageLoop();

	systemMgr.removeTray();

	return (INT) result;
}