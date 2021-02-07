#include "wndproc.h"
#include "tray.h"

extern HWND g_hWnd;
extern HINSTANCE g_hInstance;
extern volatile DWORD limitPercent;
extern volatile DWORD limitWorking;
extern volatile bool HijackThreadWaiting;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			HMENU hMenu = CreatePopupMenu();
			POINT pt;
			if (HijackThreadWaiting) {
				AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制工具 - 等待游戏运行");
			} else {
				AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制工具 - 侦测到SGuard");
			}
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT90, "限制资源：90%（默认）");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT95, "限制资源：95%");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT99, "限制资源：99%");
			AppendMenu(hMenu, MFT_STRING, IDM_PERCENT999, "限制资源：99.9%（谨慎使用）");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT, "停止限制SGuard行为");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
			if (limitWorking) {
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
			TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
			DestroyMenu(hMenu);
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
			limitWorking = 0;
			while (!HijackThreadWaiting); // spin; wait till hijack release target thread.
			PostMessage(g_hWnd, WM_CLOSE, 0, 0);
		}
			break;
		case IDM_TITLE:
		{
			// show about dialog.
			MessageBox(0,
				"本工具启动后自动探测并限制后台的SGuard。\n"
				"该工具仅供研究交流dnf优化使用，目的为提供更好的游戏环境，并且将来可能失效。\n"
				"若您发现该工具失效，请停止使用并等待论坛更新。\n\n"
				"注意：谨慎使用较高的限制等级。若您几乎完全限制SGuard的行为，则游戏可能发生异常。\n",
				"SGuard限制工具 21.2.7  colg@H3d9",
				MB_OK);
		}
			break;
		case IDM_PERCENT90:
			limitWorking = 1;
			limitPercent = 90;
			break;
		case IDM_PERCENT95:
			limitWorking = 1;
			limitPercent = 95;
			break;
		case IDM_PERCENT99:
			limitWorking = 1;
			limitPercent = 99;
			break;
		case IDM_PERCENT999:
			limitWorking = 1;
			limitPercent = 999;
			break;
		case IDM_STOPLIMIT:
			limitWorking = 0;
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