#include <Windows.h>
#include <stdio.h>
#include "wndproc.h"
#include "resource.h"
#include "win32utility.h"
#include "config.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"

extern volatile bool            g_HijackThreadWaiting;
extern volatile DWORD           g_Mode;

extern win32SystemManager&      systemMgr;
extern ConfigManager&           configMgr;
extern LimitManager&            limitMgr;
extern TraceManager&            traceMgr;
extern PatchManager&            patchMgr;


// about func: show about dialog box.
static void ShowAbout() {
	MessageBox(0,
		"本工具启动后自动优化后台ACE-Guard Client EXE的资源占用。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"如果你发现无法正常使用，请更换模式或选项；若还不行请停止使用，并将遇到的错误反馈至下方链接。\n\n"
		"【工作模式说明】\n\n"
		"1 时间片轮转（21.2.6）：\n"
		"这是最早的模式，容易出问题，不建议使用。\n\n"
		"2 线程追踪（21.7.17）：\n"
		"【注】建议你优先使用模式3。若模式3不能用再使用该模式。\n"
		"根据统计反馈，目前该模式中最好用的选项为【弱锁定-rr】。\n"
		"如果出现问题，可以点击【设置时间切分】，并尝试90，85，80...直到合适即可。\n"
		"【时间切分】设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n\n"
		"3 Memory Patch（21.10.6）：\n"
		"  >1 NtQueryVirtualMemory: 令SGUARD扫内存的速度变慢。\n"
		"  （若只开这一项，理论上并不会出现游戏异常。）\n"
		"  >2 GetAsyncKeyState: 令SGUARD读取键盘按键的速度变慢。\n"
		"  （虽然我并不知道为何它会频繁读取键盘按键，但该项似乎能有效提升游戏流畅度。）\n"
		"  （与之相关的引用位于SGUARD使用的动态库ACE-DRV64.dll中。 ）\n"
		"  >3 NtWaitForSingleObject:（旧版增强模式）已知可能导致游戏异常，不建议使用。\n"
		"  （某些机器可以正常使用该项。如果你想使用，不建议设置太大的数值。）\n"
		"  >4 NtDelayExecution:（旧版功能）已知可能导致游戏异常和卡顿，不建议使用。\n"
		"  （如果你想用这个，建议取消勾选其余两项，并且不建议设置太大的数值。）\n\n"
		"  Memory Patch需要临时装载一次驱动，提交内存后会立即将之卸载。\n"
		"  若你使用时出现问题，可以去下方链接下载证书。\n\n\n"
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

	static DWORD id = 0;

	auto& delayRange = patchMgr.patchDelayRange;
	auto& delay      = patchMgr.patchDelay;

	switch (message) {
		case WM_INITDIALOG:
		{
			id = (DWORD)lParam;

			char buf[128];
			sprintf(buf, "输入%u~%u的整数（当前值：%u）", delayRange[id].low, delayRange[id].high, delay[id]);
			SetDlgItemText(hDlg, IDC_SETDELAYTEXT, buf);

			if (lParam == 0) {
				SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtQueryVirtualMemory");
			} else if (lParam == 1) {
				SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：GetAsyncKeyState");
			} else if (lParam == 2) {
				SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtWaitForSingleObject\n【注意】不建议设置大于100的数值。");
			} else { // if lParam == 3
				SetDlgItemText(hDlg, IDC_SETDELAYNOTE, "当前设置：NtDelayExecution");
			}

			return (INT_PTR)TRUE;
		}

		case WM_COMMAND:
		{
			if (LOWORD(wParam) == IDC_SETDELAYOK) {
				BOOL translated;
				UINT res = GetDlgItemInt(hDlg, IDC_SETDELAYEDIT, &translated, FALSE);

				if (!translated || res < delayRange[id].low || res > delayRange[id].high) {
					systemMgr.panic("应输入%u~%u的整数", delayRange[id].low, delayRange[id].high);
					return (INT_PTR)FALSE;
				} else {
					patchMgr.patchDelay[id] = res;
					EndDialog(hDlg, LOWORD(wParam));
					return (INT_PTR)TRUE;
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
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			
			CHAR    buf   [128] = {};
			HMENU   hMenu       = CreatePopupMenu();

			if (g_Mode == 0) {
				if (!limitMgr.limitEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 侦测到SGuard");
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：时间片轮转  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT90, "限制资源：90%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT95, "限制资源：95%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT99, "限制资源：99%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT999, "限制资源：99.9%");
				AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT, "停止限制");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				if (limitMgr.limitEnabled) {
					switch (limitMgr.limitPercent) {
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
			} else if (g_Mode == 1) {
				if (!traceMgr.lockEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else {
					if (traceMgr.lockPid == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 正在分析");
					} else {
						sprintf(buf, "SGuard限制器 - ");
						switch (traceMgr.lockMode) {
						case 0:
							for (auto i = 0; i < 3; i++) {
								sprintf(buf + strlen(buf), "%x[%c] ", traceMgr.lockedThreads[i].tid, traceMgr.lockedThreads[i].locked ? 'O' : 'X');
							}
							break;
						case 1:
							for (auto i = 0; i < 3; i++) {
								sprintf(buf + strlen(buf), "%x[..] ", traceMgr.lockedThreads[i].tid);
							}
							break;
						case 2:
							sprintf(buf + strlen(buf), "%x[%c] ", traceMgr.lockedThreads[0].tid, traceMgr.lockedThreads[0].locked ? 'O' : 'X');
							break;
						case 3:
							sprintf(buf + strlen(buf), "%x[..] ", traceMgr.lockedThreads[0].tid);
							break;
						}
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, buf);
					}
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：线程追踪  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3, "锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1, "弱锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3RR, "锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1RR, "弱锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK, "解除锁定");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				if (traceMgr.lockMode == 1 || traceMgr.lockMode == 3) {
					sprintf(buf, "设置时间切分（当前：%d）", traceMgr.lockRound);
					AppendMenu(hMenu, MFT_STRING, IDM_SETRRTIME, buf);
					AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				}
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				if (traceMgr.lockEnabled) {
					switch (traceMgr.lockMode) {
					case 0:
						CheckMenuItem(hMenu, IDM_LOCK3, MF_CHECKED);
						break;
					case 1:
						CheckMenuItem(hMenu, IDM_LOCK3RR, MF_CHECKED);
						break;
					case 2:
						CheckMenuItem(hMenu, IDM_LOCK1, MF_CHECKED);
						break;
					case 3:
						CheckMenuItem(hMenu, IDM_LOCK1RR, MF_CHECKED);
						break;
					}
				} else {
					CheckMenuItem(hMenu, IDM_UNLOCK, MF_CHECKED);
				}
			} else {
				if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else {
					DWORD total = 0;
					if (patchMgr.patchSwitches.NtQueryVirtualMemory  ||
						patchMgr.patchSwitches.NtWaitForSingleObject ||
						patchMgr.patchSwitches.NtDelayExecution) {
						total ++;
					}
					if (patchMgr.patchSwitches.GetAsyncKeyState) {
						total ++;
					}

					DWORD finished = 0;
					if (patchMgr.patchStatus.stage1) {
						finished ++;
					}
					if (patchMgr.patchStatus.stage2) {
						finished ++;
					}

					if (finished == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 请等待");
					} else {
						sprintf(buf, "SGuard限制器 - 已提交  [%u/%u]", finished, total);
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, buf);
					}
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：MemPatch V3  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_DOPATCH, "自动");
				AppendMenu(hMenu, MF_GRAYED, IDM_UNDOPATCH, "撤销修改");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH1, "inline Ntdll!NtQueryVirtualMemory");
				AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH2, "inline User32!GetAsyncKeyState");
				AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH3, "inline Ntdll!NtWaitForSingleObject");
				AppendMenu(hMenu, MFT_STRING, IDM_PATCHSWITCH4, "re-write Ntdll!NtDelayExecution");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				sprintf(buf, "设置延时（当前：%u/%u/%u/%u）", patchMgr.patchDelay[0], patchMgr.patchDelay[1], patchMgr.patchDelay[2], patchMgr.patchDelay[3]);
				AppendMenu(hMenu, MFT_STRING, IDM_SETDELAY, buf);
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				CheckMenuItem(hMenu, IDM_DOPATCH, MF_CHECKED);
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH1, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH2, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH3, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.NtDelayExecution) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH4, MF_CHECKED);
				}
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
				configMgr.writeConfig();
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
				configMgr.writeConfig();
				break;
			case IDM_PERCENT95:
				limitMgr.setPercent(95);
				configMgr.writeConfig();
				break;
			case IDM_PERCENT99:
				limitMgr.setPercent(99);
				configMgr.writeConfig();
				break;
			case IDM_PERCENT999:
				limitMgr.setPercent(999);
				configMgr.writeConfig();
				break;
			case IDM_STOPLIMIT:
				limitMgr.disable();
				break;
			
			// lock command
			case IDM_LOCK3:
				traceMgr.setMode(0);
				configMgr.writeConfig();
				break;
			case IDM_LOCK3RR:
				traceMgr.setMode(1);
				configMgr.writeConfig();
				break;
			case IDM_LOCK1:
				traceMgr.setMode(2);
				configMgr.writeConfig();
				break;
			case IDM_LOCK1RR:
				traceMgr.setMode(3);
				configMgr.writeConfig();
				break;
			case IDM_SETRRTIME:
				DialogBox(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETTIMEDIALOG), hWnd, SetTimeDlgProc);
				configMgr.writeConfig();
				break;
			case IDM_UNLOCK:
				traceMgr.disable();
				break;
				
			// patch command
			case IDM_SETDELAY:
				if (IDYES == MessageBox(0, "请依次设置以下开关的强制延时。\n如果不想设置某个选项，可以直接关掉对应的窗口。\n\nNtQueryVirtualMemory\nGetAsyncKeyState\nNtWaitForSingleObject\nNtDelayExecution\n", "信息", MB_YESNO)) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 0);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 1);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 2);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 3);
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					configMgr.writeConfig();
				}
				break;
			case IDM_PATCHSWITCH1:
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					patchMgr.patchSwitches.NtQueryVirtualMemory = false;
				} else {
					patchMgr.patchSwitches.NtQueryVirtualMemory = true;
				}
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				configMgr.writeConfig();
				break;
			case IDM_PATCHSWITCH2:
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					patchMgr.patchSwitches.GetAsyncKeyState = false;
				} else {
					patchMgr.patchSwitches.GetAsyncKeyState = true;
				}
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				configMgr.writeConfig();
				break;
			case IDM_PATCHSWITCH3:
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					patchMgr.patchSwitches.NtWaitForSingleObject = false;
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				} else {
					if (IDYES == MessageBox(0, "这是旧版增强模式，已知可能导致游戏异常。如果你出现“3009”，“96”，“lol掉线”问题，需要立即关闭该选项。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtWaitForSingleObject = true;
						MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					}
				}
				configMgr.writeConfig();
				break;
			case IDM_PATCHSWITCH4:
				if (patchMgr.patchSwitches.NtDelayExecution) {
					patchMgr.patchSwitches.NtDelayExecution = false;
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				} else {
					if (IDYES == MessageBox(0, "这是旧版功能，不建议启用该选项，以免出现“3009”，“96”，“偶尔卡顿”等问题。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtDelayExecution = true;
						MessageBox(0, "重启游戏后生效", "注意", MB_OK);
					}
				}
				configMgr.writeConfig();
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