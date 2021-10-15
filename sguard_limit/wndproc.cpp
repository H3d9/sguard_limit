#include <Windows.h>
#include <stdio.h>
#include "win32utility.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"
#include "resource.h"

#include "wndproc.h"

extern volatile bool            g_bHijackThreadWaiting;

extern volatile DWORD           g_Mode;

extern win32SystemManager&      systemMgr;

extern LimitManager&            limitMgr;
extern TraceManager&            traceMgr;
extern PatchManager&            patchMgr;


// about func: show about dialog box.
static void ShowAbout() {
	MessageBox(0,
		"本工具启动后自动优化后台ACE-Guard Client EXE的资源占用。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"如果你发现无法正常使用，请更换模式或选项；若还不行请停止使用，并将遇到的错误反馈至下方链接。\n\n"
		"工作模式说明：\n"
		"1 时间片轮转（21.2.6）：已知可能导致“96-0”，若出现该情况可切换至【线程跟踪】。\n"
		"  (当然，即使没有出现错误你也可以使用其他模式，不会有任何影响)\n\n"
		"2 线程追踪（21.7.17）：已知部分机器使用“锁定”选项时会出现“3009-0”，若出现该情况可以尝试【锁定-rr】。\n"
		"如果你使用【锁定-rr】依旧出问题，可点击【设置时间切分】，并尝试较小的时间。例如尝试90，85，80...直到合适即可。\n"
		"注：【时间切分】设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n\n"
		"3 Memory Patch（21.10.6）：\n"
		"NtQueryVirtualMemory: 只挂钩SGUARD扫内存的系统调用，理论上并不会出现游戏异常。\n"
		"NtWaitForSingleObject: 增强模式，若调较大的数值配合上面可以让SGUARD占用接近0，但不清楚是否会出游戏异常。\n"
		"【注意】如果出现游戏异常，建议先关闭这一项。\n"
		"NtDelayExecution:（旧版功能）不建议开启，以免可能出现游戏异常或偶尔卡顿的问题。如果你想用这个，建议取消勾选其余两项。\n"
		"【注意】模式3需要临时装载一次驱动（提交更改后会立即将之卸载）。若你使用时出现问题，可以去论坛链接下载证书。\n\n\n"
		"SGUARD讨论群：775176979\n\n"
		"论坛链接：https://bbs.colg.cn/thread-8087898-1-1.html \n"
		"项目地址：https://github.com/H3d9/sguard_limit （点ctrl+C复制到记事本）",
		"SGuard限制器 " VERSION "  colg@H3d9",
		MB_OK);
}


// dialog: set time slice.
static INT_PTR CALLBACK SetTimeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {
	case WM_INITDIALOG:
	{
		char buf[128];
		sprintf(buf, "输入1~99的整数（当前值：%d）", traceMgr.lockRound);
		SetDlgItemText(hDlg, IDC_SETTIMETEXT, buf);
			return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SETTIMEOK) {
			BOOL translated;
			UINT res = GetDlgItemInt(hDlg, IDC_SETTIMEEDIT, &translated, FALSE);
			if (!translated || res < 1 || res > 99) {
				MessageBox(0, "输入1~99的整数", "错误", MB_OK);
			} else {
				traceMgr.lockRound = res;
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
		}
		break;

	case WM_CLOSE:
		EndDialog(hDlg, LOWORD(wParam));
		return (INT_PTR)TRUE;
	}

	return (INT_PTR)FALSE;
}


// dialog: set syscall delay.
static INT_PTR CALLBACK SetDelayDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	static int mode = 0;

	switch (message) {
	case WM_INITDIALOG:
	{
		char buf[128];
		if (lParam == 0) {
			mode = 0;
			sprintf(buf, "输入200~2000的整数（当前值：%u）", patchMgr.patchDelay[0]);
			SetDlgItemText(hDlg, IDC_SETDELAYTEXT, buf);
			SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtQueryVirtualMemory");
		} else if (lParam == 1) {
			mode = 1;
			sprintf(buf, "输入500~5000的整数（当前值：%u）", patchMgr.patchDelay[1]);
			SetDlgItemText(hDlg, IDC_SETDELAYTEXT, buf);
			SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtWaitForSingleObject");
		} else { // if lParam == 2
			mode = 2;
			sprintf(buf, "输入200~2000的整数（当前值：%u）", patchMgr.patchDelay[2]);
			SetDlgItemText(hDlg, IDC_SETDELAYTEXT, buf);
			SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtDelayExecution");
		}
		return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
	{
		if (LOWORD(wParam) == IDC_SETDELAYOK) {
			BOOL translated;
			UINT res = GetDlgItemInt(hDlg, IDC_SETDELAYEDIT, &translated, FALSE);

			if (mode == 0) {
				if (!translated || res < 200 || res > 2500) {
					MessageBox(0, "输入200~2500的整数", "错误", MB_OK);
				} else {
					patchMgr.patchDelay[0] = res;
					EndDialog(hDlg, LOWORD(wParam));
					return (INT_PTR)TRUE;
				}
			} else if (mode == 1) {
				if (!translated || res < 500 || res > 5000) {
					MessageBox(0, "输入500~5000的整数", "错误", MB_OK);
				} else {
					patchMgr.patchDelay[1] = res;
					EndDialog(hDlg, LOWORD(wParam));
					return (INT_PTR)TRUE;
				}
			} else { // if mode == 2
				if (!translated || res < 200 || res > 2000) {
					MessageBox(0, "输入200~2000的整数", "错误", MB_OK);
				} else {
					patchMgr.patchDelay[2] = res;
					EndDialog(hDlg, LOWORD(wParam));
					return (INT_PTR)TRUE;
				}
			}
		}
	}
		break;

	case WM_CLOSE:
	{
		EndDialog(hDlg, LOWORD(wParam));
		return (INT_PTR)TRUE;
	}
	}

	return (INT_PTR)FALSE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	switch (msg) {
	case win32SystemManager::WM_TRAYACTIVATE:     // fall in tray, pre-defined and set in sysMgr
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			
			HMENU hMenu = CreatePopupMenu();
			
			if (g_Mode == 0) {
				limitMgr.wndProcAddMenu(hMenu);
			} else if (g_Mode == 1) {
				traceMgr.wndProcAddMenu(hMenu);
			} else {
				patchMgr.wndProcAddMenu(hMenu);
			}
			
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
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
			// general command
			case IDM_TITLE:
				ShowAbout();
				break;
			case IDM_SWITCHMODE:
				if (g_Mode == 0) {
					limitMgr.disable();
					traceMgr.enable();
					g_Mode = 1;
				} else if (g_Mode == 1) {
					traceMgr.disable();
					patchMgr.enable();
					g_Mode = 2;
				} else {
					patchMgr.disable();
					limitMgr.enable();
					g_Mode = 0;
				}
				systemMgr.writeConfig();
				break;
			case IDM_EXIT:
			{
				if (g_Mode == 0 && limitMgr.limitEnabled) {
					limitMgr.disable();
				} else if (g_Mode == 1 && traceMgr.lockEnabled) {
					traceMgr.disable();
				} else if (g_Mode == 2 && patchMgr.patchEnabled) {
					patchMgr.disable();
				}
				PostMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			// limit command
			case IDM_PERCENT90:
				limitMgr.setPercent(90);
				systemMgr.writeConfig();
				break;
			case IDM_PERCENT95:
				limitMgr.setPercent(95);
				systemMgr.writeConfig();
				break;
			case IDM_PERCENT99:
				limitMgr.setPercent(99);
				systemMgr.writeConfig();
				break;
			case IDM_PERCENT999:
				limitMgr.setPercent(999);
				systemMgr.writeConfig();
				break;
			case IDM_STOPLIMIT:
				limitMgr.disable();
				break;
			
			// lock command
			case IDM_LOCK3:
				traceMgr.setMode(0);
				systemMgr.writeConfig();
				break;
			case IDM_LOCK3RR:
				traceMgr.setMode(1);
				systemMgr.writeConfig();
				break;
			case IDM_LOCK1:
				traceMgr.setMode(2);
				systemMgr.writeConfig();
				break;
			case IDM_LOCK1RR:
				traceMgr.setMode(3);
				systemMgr.writeConfig();
				break;
			case IDM_SETRRTIME:
				DialogBox(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETTIMEDIALOG), hWnd, SetTimeDlgProc);
				systemMgr.writeConfig();
				break;
			case IDM_UNLOCK:
				traceMgr.disable();
				break;
				
			// patch command
			case IDM_DOPATCH:
				patchMgr.enable(true);
				break;
			case IDM_UNDOPATCH:
				if (MessageBox(0, "提交修改后最好让目标保持原样（即重启游戏时让他自动关闭）。\n不建议使用该功能，除非你知道自己在做什么，仍要继续么？", "注意", MB_YESNO) == IDYES) {
					patchMgr.disable(true);
				}
				break;
			case IDM_SETDELAY:
				if (IDYES == MessageBox(0, "请依次设置以下函数的强制延时。\n如果不想设置某个选项，可以直接关掉对应的窗口。\n\nNtQueryVirtualMemory\nNtWaitForSingleObject\nNtDelayExecution\n", "信息", MB_YESNO)) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 0);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 1);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 2);
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					systemMgr.writeConfig();
				}
				break;
			case IDM_PATCHSWITCH1:
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					patchMgr.patchSwitches.NtQueryVirtualMemory = false;
				} else {
					patchMgr.patchSwitches.NtQueryVirtualMemory = true;
				}
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				systemMgr.writeConfig();
				break;
			case IDM_PATCHSWITCH2:
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					patchMgr.patchSwitches.NtWaitForSingleObject = false;
				} else {
					if (IDYES == MessageBox(0, "这是增强模式，如果你出现“3009”，“96”问题，请立即关闭该选项。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtWaitForSingleObject = true;
						MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					}
				}
				systemMgr.writeConfig();
				break;
			case IDM_PATCHSWITCH3:
				if (patchMgr.patchSwitches.NtDelayExecution) {
					patchMgr.patchSwitches.NtDelayExecution = false;
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				} else {
					if (IDYES == MessageBox(0, "这是旧版功能，如果你之前出现“3009”，“96”，“偶尔卡顿”等问题，不要启用该选项。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtDelayExecution = true;
						MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					}
				}
				systemMgr.writeConfig();
				break;
		}
	}
	break;
	case WM_CLOSE:
	{
		DestroyWindow(hWnd);
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