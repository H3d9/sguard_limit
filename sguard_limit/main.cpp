// SGuard64 行为限制工具
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include "tray.h"
#include "panic.h"
#include "config.h"
#include "wndproc.h"
#include "limitcore.h"
#include "tracecore.h"
#include "resource.h"

extern volatile	bool limitEnabled;
extern volatile	bool lockEnabled;

HWND				g_hWnd					= NULL;
HINSTANCE			g_hInstance				= NULL;
volatile bool		g_bHijackThreadWaiting	= true;

volatile DWORD		g_Mode					= 1;  // 0: lim 1: lock


static ATOM RegisterMyClass() {

	WNDCLASS wc = {0};

	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = &WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = g_hInstance;
	wc.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = "SGuardLimit_WindowClass";

	return RegisterClass(&wc);
}

static void EnableDebugPrivilege() {

	HANDLE hToken;
	LUID Luid;
	TOKEN_PRIVILEGES tp;

	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &Luid);

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = Luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);

	CloseHandle(hToken);
}

// 另一种提权方法（使用未公开接口）
//static void Enable_se_debug() { // stdcall convention declaration can be omitted if use x64.
//	typedef int(__stdcall* pf)(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);
//	pf RtlAdjustPrivilege = (pf)GetProcAddress(GetModuleHandle("Ntdll.dll"), "RtlAdjustPrivilege");
//	BOOLEAN prev;
//	int ret = RtlAdjustPrivilege(0x14, 1, 0, &prev);
//}

static BOOL CheckDebugPrivilege() {

	HANDLE hToken;
	LUID luidPrivilege = { 0 };
	PRIVILEGE_SET RequiredPrivileges = { 0 };
	BOOL bResult = 0;

	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidPrivilege);

	RequiredPrivileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
	RequiredPrivileges.PrivilegeCount = 1;
	RequiredPrivileges.Privilege[0].Luid = luidPrivilege;
	RequiredPrivileges.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

	PrivilegeCheck(hToken, &RequiredPrivileges, &bResult);

	CloseHandle(hToken);

	return bResult;
}

static DWORD WINAPI HijackThreadWorker(LPVOID) {

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	while (1) {
		// scan per 5 seconds when idle; if process is found, trap into hijack()。
		DWORD pid = GetProcessID();
		if (pid) {
			if (g_Mode == 0 && limitEnabled) {
				g_bHijackThreadWaiting = false; // sync is done as we call schedule
				Hijack(pid); // start hijack.
				g_bHijackThreadWaiting = true;
			}
			if (g_Mode == 1 && lockEnabled) {
				g_bHijackThreadWaiting = false;
				threadChase(pid);
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

	g_hInstance = hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;

	if (!RegisterMyClass()) {
		panic("创建窗口类失败。");
		return -1;
	}

	g_hWnd = CreateWindow(
		"SGuardLimit_WindowClass",
		"SGuardLimit_Window",
		WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, 0, 0, g_hInstance, 0);

	if (!g_hWnd) {
		panic("创建窗口失败。");
		return -1;
	}

	ShowWindow(g_hWnd, SW_HIDE);

	EnableDebugPrivilege();
	if (!CheckDebugPrivilege()) {
		panic("提升权限失败，请右键管理员运行。");
		return -1;
	}

	CreateTray();
	
	if (!loadConfig()) {
		MessageBox(0,
			"首次使用说明：\n"
			"修复旧版【锁定-rr】依旧出现“3009-0”的问题，若你出现该情况请点击【设置时间切分】，并设置一个更小的时间。\n\n"
			"【提示】双击右下角托盘图标，可以查看新版详细说明。",
			VERSION " colg@H3d9", MB_OK);
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	HANDLE hijackThread = CreateThread(NULL, NULL, HijackThreadWorker, NULL, 0, 0);
	if (!hijackThread) {
		panic("创建工作线程失败。");
		return -1;
	}
	CloseHandle(hijackThread);


	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0)) 
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}


	RemoveTray();

	return (INT) msg.wParam;
}