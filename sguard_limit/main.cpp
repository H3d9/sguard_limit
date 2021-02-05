// SGuard64 限制工具
// H3d9, 写于2021.2.5晚。

#include <Windows.h>
#include "tray.h"
#include "wndproc.h"
#include "limitcore.h"
#include "resource.h"

HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;
volatile bool HijackThreadWaiting = true;


ATOM RegisterMyClass() {
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

void EnableDebugPrivilege()
{
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

BOOL CheckDebugPrivilege() {
	HANDLE hToken;

	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
	LUID luidPrivilege = { 0 };
	PRIVILEGE_SET RequiredPrivileges = { 0 };
	BOOL bResult = 0;

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidPrivilege);

	RequiredPrivileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
	RequiredPrivileges.PrivilegeCount = 1;
	RequiredPrivileges.Privilege[0].Luid = luidPrivilege;
	RequiredPrivileges.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

	PrivilegeCheck(hToken, &RequiredPrivileges, &bResult);

	return bResult;
}

DWORD WINAPI HijackCreater(LPVOID param) {

	static int failCount = 0;

	while (1) {
		// scan per 1 second when idle; if process is found, trap into hijack()。
		DWORD pid = GetProcessID("SGuard64.exe");
		if (pid) {
			HijackThreadWaiting = false;
			if (!Hijack(pid)) { // start hijack.
				++failCount;
				if (failCount == 10) {
					MessageBox(0, "限制资源可能未成功；请观察任务管理器以检查限制是否生效。", 0, MB_OK);
				}
			} else {
				failCount = 0; // process terminated, or user stopped limitation.
			}
			HijackThreadWaiting = true;
		} else {
			Sleep(1000); // no target found, wait.
		}
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
		MessageBox(0, "创建窗口类失败", 0, MB_OK);
		return -1;
	}

	g_hWnd = CreateWindow(
		"SGuardLimit_WindowClass",
		"SGuardLimit_Window",
		WS_EX_TOPMOST,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1,
		1,
		0,
		0,
		g_hInstance,
		0);

	if (!g_hWnd) {
		MessageBox(0, "创建窗口失败", 0, MB_OK);
		return -1;
	}

	ShowWindow(g_hWnd, SW_HIDE);

	EnableDebugPrivilege();
	if (!CheckDebugPrivilege()) {
		MessageBox(0, "提升权限失败，请右键管理员运行", 0, MB_OK);
		return -1;
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	CreateTray();
	
	// 
	HANDLE hijackThread = CreateThread(NULL, NULL, HijackCreater, NULL, 0, 0);
	if (!hijackThread) {
		MessageBox(0, "创建线程失败", 0, MB_OK);
		return -1;
	}
	CloseHandle(hijackThread);

	// assert: se_debug
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	RemoveTray();

	return msg.wParam;
}