#include <stdio.h>
#include "wndproc.h"
#include "tray.h"
#include "tlockcore.h"

extern HWND g_hWnd;
extern HINSTANCE g_hInstance;
extern volatile bool HijackThreadWaiting;
extern volatile int g_Mode;

extern volatile bool limitEnabled;
extern volatile DWORD limitPercent;

extern volatile bool lockEnabled;
extern volatile lockedThreads_t lockedThreads[3];
extern volatile DWORD lockPid;

static void ShowAbout() {  // show about dialog.
	MessageBox(0,
		"本工具启动后自动优化后台SGuard的资源占用。\n"
		"该工具仅供研究交流dnf优化使用，将来可能失效，不保证稳定性。\n"
		"若您发现该工具已无法正常使用，请更换模式；若还不行请停止使用并等待论坛更新。\n\n"
		"工作模式说明：\n"
		"1 时间片轮转（旧模式）：如果你可以正常使用该模式，继续使用即可。当然你也可以使用新模式。\n"
		"2 线程锁（新模式）：一些机器使用旧模式会出现安全数据上报异常(96)，可以尝试该模式，经测试基本确定不会出现问题（但不保证，因为手头电脑有限）。\n\n"
		"更新链接：https://bbs.colg.cn/thread-8087898-1-1.html \n"
		"项目地址：https://github.com/H3d9/sguard_limit （点ctrl+C复制到记事本）",
		"SGuard限制器 21.7.21  colg@H3d9",
		MB_OK);
}

static void disableLimit() {  // undo control.
	limitEnabled = false;
	while (!HijackThreadWaiting); // spin; wait till hijack release target thread.
}

static void disableLock() {  // undo control.
	lockEnabled = false;
	for (auto i = 0; i < 3; i++) {
		if (lockedThreads[i].locked) {
			ResumeThread(lockedThreads[i].handle);
			lockedThreads[i].locked = false;
		}
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			HMENU hMenu = CreatePopupMenu();
			POINT pt;
			if (g_Mode == 0) {
				if (!limitEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (HijackThreadWaiting) {
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
				CheckMenuItem(hMenu, IDM_PERCENT90, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT95, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT99, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT999, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_UNCHECKED);
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
			} else { // if g_Mode == 1
				if (!lockEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else { // entered func: threadLock()
					if (lockPid == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 正在分析");
					} else {
						char titleBuf[512] = "SGuard限制器 - 已锁定：";
						for (auto i = 0; i < 3; i++) {
							sprintf(titleBuf + strlen(titleBuf), "%x(%d) ", lockedThreads[i].tid, i);
						}
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, titleBuf);
					}
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：线程锁 [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3, "锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK, "解除锁定");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				CheckMenuItem(hMenu, IDM_LOCK3, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_UNLOCK, MF_UNCHECKED);
				if (lockEnabled) {
					CheckMenuItem(hMenu, IDM_LOCK3, MF_CHECKED);
				} else {
					CheckMenuItem(hMenu, IDM_UNLOCK, MF_CHECKED);
				}
			}
			GetCursorPos(&pt);
			SetForegroundWindow(g_hWnd);
			TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
			DestroyMenu(hMenu);
		} else if (lParam == WM_LBUTTONDBLCLK) {
			ShowAbout();
		}
	}
	break;
	case WM_COMMAND:
	{
		UINT id = LOWORD(wParam);
		switch (id) {
		case IDM_EXIT:
		{
			if (g_Mode == 0 && limitEnabled == true) {
				disableLimit();
			} else if (g_Mode == 1 && lockEnabled == true) {
				disableLock();
			}
			PostMessage(g_hWnd, WM_CLOSE, 0, 0);
		}
			break;
		case IDM_TITLE:
			ShowAbout();
			break;
		case IDM_SWITCHMODE:
			if (g_Mode == 0) {
				disableLimit();
				lockEnabled = true;
				g_Mode = 1;
			} else { // if g_Mode == 1
				disableLock();
				limitEnabled = true;
				g_Mode = 0;
			}
			break;

		case IDM_PERCENT90:
			limitEnabled = true;
			limitPercent = 90;
			break;
		case IDM_PERCENT95:
			limitEnabled = true;
			limitPercent = 95;
			break;
		case IDM_PERCENT99:
			limitEnabled = true;
			limitPercent = 99;
			break;
		case IDM_PERCENT999:
			limitEnabled = true;
			limitPercent = 999;
			break;
		case IDM_STOPLIMIT:
			limitEnabled = false;
			limitPercent = 90;
			break;

		case IDM_LOCK3:
			lockEnabled = true;
			break;
		case IDM_UNLOCK:
			disableLock();
			break;
		}
	}
	break;
	case WM_CLOSE:
	{
		DestroyWindow(g_hWnd);
	}
	break;
	case WM_DESTROY:
	{
		PostQuitMessage(0);
	}
	break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}