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
				g_HijackThreadWaiting = false;    // sync is done as we call schedule
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

	systemMgr.enableDebugPrivilege();

	status =
	systemMgr.checkDebugPrivilege();

	if (!status) {
		return -1;
	}

	status =
	systemMgr.init(hInstance, IDI_ICON1, WM_TRAYACTIVATE);

	if (!status) {
		return -1;
	}

	status = 
	systemMgr.createWin32Window(WndProc);
	
	if (!status) {
		return -1;
	}

	systemMgr.createTray();

	configMgr.init(systemMgr.profilePath());

	patchMgr.init();

	status = 
	configMgr.loadConfig();

	if (!status) {
		MessageBox(0,
			"【首次使用说明】\n\n"
			"更新模式：MemPatch V3\n\n"
			"【新特性】在上一版的基础上进一步压缩SGUARD的cpu使用率令其接近0。\n（但必要时SGUARD仍会短暂占用cpu以防游戏出现异常）\n\n"
			"1 新增定位点 GetAsyncKeyState。\n\n"
			"2 删除上述定位点的错误提示（因无关紧要），并增加其重试次数和搜索范围。\n\n"
			"3 修复旧版（21.10.16/17）在【win7】/【win11】下无效的问题。\n"
			"  (特别感谢@白嫖怪 提供的远程win11系统)\n\n\n"
			"【重要提示】若你第一次使用，建议双击右下角托盘图标，并仔细阅读说明。\n",
			VERSION " colg@H3d9", MB_OK);	
		if (IDYES == MessageBox(0, "要查看网页版说明么？", "提示", MB_YESNO)) {
			ShellExecute(0, "open", "https://bbs.colg.cn/thread-8305966-1-1.html", 0, 0, SW_SHOW);
		}
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