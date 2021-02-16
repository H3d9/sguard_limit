#include "wndproc.h"
#include "tray.h"

extern HWND g_hWnd;
extern HINSTANCE g_hInstance;
extern volatile DWORD limitPercent;
extern volatile bool limitEnabled;
extern volatile bool HijackThreadWaiting;

void ShowAbout() {  // show about dialog.
	MessageBox(0,
		"本工具启动后自动探测并限制后台的SGuard。\n"
		"该工具仅供研究交流dnf优化使用，目的为提供更好的游戏环境，并且将来可能失效。\n"
		"若您发现该工具失效，请停止使用并等待论坛更新。\n\n"
		"注意：若您的电脑性能较差，请谨慎使用较高的限制等级。若SGuard无法上报数据，则游戏可能发生异常。"
		"（性能较好的可忽略）\n",
		"SGuard行为限制工具 21.2.16  colg@H3d9",
		MB_OK);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			HMENU hMenu = CreatePopupMenu();
			POINT pt;
			if (!limitEnabled) {
				AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制工具 - 用户停止限制");
			} else if (HijackThreadWaiting) {
				AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制工具 - 等待游戏运行");
			} else {
				AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制工具 - 侦测到SGuard");
			}
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT90, "限制资源：90%（默认）");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT95, "限制资源：95%");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT99, "限制资源：99%");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT999, "限制资源：99.9%");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT, "停止限制SGuard行为");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
			if (limitEnabled) {
				switch (limitPercent) {
				case 90:
					CheckMenuItem(hMenu, IDM_PERCENT90, MF_CHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT95, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT99, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT999, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_UNCHECKED);
					break;
				case 95:
					CheckMenuItem(hMenu, IDM_PERCENT90, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT95, MF_CHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT99, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT999, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_UNCHECKED);
					break;
				case 99:
					CheckMenuItem(hMenu, IDM_PERCENT90, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT95, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT99, MF_CHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT999, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_UNCHECKED);
					break;
				case 999:
					CheckMenuItem(hMenu, IDM_PERCENT90, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT95, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT99, MF_UNCHECKED);
					CheckMenuItem(hMenu, IDM_PERCENT999, MF_CHECKED);
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_UNCHECKED);
					break;
				}
			} else {
				CheckMenuItem(hMenu, IDM_PERCENT90, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT95, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT99, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_PERCENT999, MF_UNCHECKED);
				CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_CHECKED);
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
			// before exit, stop limit work.
			limitEnabled = false;
			while (!HijackThreadWaiting); // spin; wait till hijack release target thread.
			PostMessage(g_hWnd, WM_CLOSE, 0, 0);
		}
			break;
		case IDM_TITLE:
			ShowAbout();
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