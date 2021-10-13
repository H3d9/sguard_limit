// SGuard64限制器，适用于各种tx游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include "win32utility.h"
#include "wndproc.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"
#include "panic.h"


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
		// scan per 5 seconds when idle; if process is found, trap into hijack()。
		DWORD pid = threadMgr.getTargetPid();
		if (pid) {
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
				patchMgr.patch();
				g_bHijackThreadWaiting = true;
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
			"注意：这是SGUARD限制器的【测试版本】。你应该把使用中遇到的问题提交至论坛。\n\n"
			"即使使用正常也建议将使用情况发送过来，以便统计。\n\n"
			"交流群：775176979\n\n",
			VERSION " colg@H3d9", MB_OK);
		ShellExecute(0, "open", "https://bbs.colg.cn/thread-8305966-1-1.html", 0, 0, SW_HIDE);
		MessageBox(0,
			"首次使用说明：\n"
			"更新模式：MemPatch V1.2\n"
			"1 修复”0x2”设备不存在的问题，如果还出问题可以把附带的sys手动拷贝到\n"
			"%appdata%\\sguard_limit文件夹。\n"
			"2 使用C++11风格对项目重构。\n"
			"\n\n"
			"【提示】双击右下角托盘图标，可以查看新版详细说明。",
			VERSION " colg@H3d9", MB_OK);
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