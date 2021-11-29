#include <Windows.h>
#include <stdio.h>
#include "wndproc.h"
#include "resource.h"
#include "kdriver.h"
#include "win32utility.h"
#include "config.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"


extern KernelDriver&            driver;
extern win32SystemManager&      systemMgr;
extern ConfigManager&           configMgr;
extern LimitManager&            limitMgr;
extern TraceManager&            traceMgr;
extern PatchManager&            patchMgr;

extern volatile bool            g_HijackThreadWaiting;
extern volatile DWORD           g_Mode;


// about func: show about dialog box.
static void ShowAbout() {
	MessageBox(0,
		"本工具用于约束TX游戏后台扫盘插件（ACE-Guard Client EXE，即SGUARD）的CPU使用率。"
		"一般情况下，你无须进行任何设置；默认选项基本上适用于所有情况。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"如果你发现无法正常使用，请更换模式或选项；若还不行请停止使用并等待更新。\n\n"
		"【注】不要直接关闭该扫盘插件，这会导致游戏掉线。\n\n\n"

		"【工作模式说明】\n\n"

		"MemPatch V3（21.10.6）：\n"
		">1 NtQueryVirtualMemory: 令SGUARD扫内存的速度变慢。\n"
		"  若只开这一项，理论上并不会导致游戏掉线。\n"
		">2 GetAsyncKeyState: 令SGUARD读取键盘按键的速度变慢。\n"
		"  虽然我并不知道为何它会频繁读取键盘按键，但该项似乎能有效提升游戏流畅度。\n"
		"  与之相关的引用位于SGUARD使用的动态库ACE-DRV64.dll中。\n"
		">3 NtWaitForSingleObject:（旧版增强模式）已知可能导致游戏异常，不建议使用。\n"
		">4 NtDelayExecution:（旧版功能）已知可能导致游戏异常和卡顿，不建议使用。\n\n"

		"【注意】Memory Patch需要临时装载一次驱动，提交内存后会立即将之卸载。若你使用时出现问题，可以去下方链接下载证书。\n\n\n"
		
		"====================================\n"
		"以下旧模式建议仅限上述新模式无法使用时再尝试\n"
		"====================================\n\n"

		"线程追踪（21.7.17）：\n"
		"根据统计反馈，目前该模式中最好用的选项为【弱锁定-rr】。\n"
		"如果出现问题，可以点击【设置时间切分】，并尝试90，85，80...直到合适即可。\n"
		"【时间切分】设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n\n"

		"时间片轮转（21.2.6）：\n"
		"这是最早的模式，其原理与BES相同，容易引起游戏掉线，不建议使用。\n"
		"若打开内核态调度，则限制器不再持有SGUARD句柄，故游戏掉线的几率相比不开要低。\n"
		"因此在你想用这个模式的前提下，建议手动打开内核态调度开关。\n\n\n"

		"SGUARD讨论群：775176979\n\n"
		"更新链接/源代码：见右键菜单→其他选项。",
		"SGuard限制器 " VERSION "  by: @H3d9",
		MB_OK);
}

// dialog: set percent.
static INT_PTR CALLBACK SetPctDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {
	case WM_INITDIALOG:
	{
		char buf[128];
		sprintf(buf, "输入整数1~99，或999（代表99.9）\n（当前值：%u）", limitMgr.limitPercent);
		SetDlgItemText(hDlg, IDC_SETPCTTEXT, buf);
		return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SETPCTOK) {
			BOOL translated;
			UINT res = GetDlgItemInt(hDlg, IDC_SETPCTEDIT, &translated, FALSE);
			if (!translated || res < 1 || (res > 99 && res != 999)) {
				MessageBox(0, "输入1~99或999", "错误", MB_OK);
			} else {
				limitMgr.setPercent(res);
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

// dialog: set time slice.
static INT_PTR CALLBACK SetTimeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {
	case WM_INITDIALOG:
	{
		char buf[128];
		sprintf(buf, "输入1~99的整数（当前值：%u）", traceMgr.lockRound);
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
			} else { // lParam == 3
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

			// for driver-depending options: 
			// auto select MFT_STRING or MF_GRAYED.
			auto    drvMenuType    = driver.driverReady ? MFT_STRING : MF_GRAYED;


			CHAR    buf      [128] = {};
			HMENU   hMenu          = CreatePopupMenu();
			HMENU   hMenuModes     = CreatePopupMenu();
			HMENU   hMenuOthers    = CreatePopupMenu();

			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_HIJACK,  "切换到：时间片轮转");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_TRACE,   "切换到：线程追踪");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_PATCH,   "切换到：MemPatch V3");
			if (g_Mode == 0) {
				CheckMenuItem(hMenuModes, IDM_MODE_HIJACK, MF_CHECKED);
			} else if (g_Mode == 1) {
				CheckMenuItem(hMenuModes, IDM_MODE_TRACE,  MF_CHECKED);
			} else { // if g_Mode == 2
				CheckMenuItem(hMenuModes, IDM_MODE_PATCH,  MF_CHECKED);
			}

			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_UPDATEPAGE, "检查更新【当前版本：" VERSION "】");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_ABOUT,           "查看说明");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_SOURCEPAGE, "查看源代码");


			if (g_Mode == 0) {
				if (!limitMgr.limitEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 用户手动暂停");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 等待游戏运行");
				} else {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 侦测到SGuard");
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：时间片轮转  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				if (limitMgr.limitPercent == 999) {
					AppendMenu(hMenu, MFT_STRING, IDM_STARTLIMIT, "限制资源：99.9%");
				} else {
					sprintf(buf, "限制资源：%u%%", limitMgr.limitPercent);
					AppendMenu(hMenu, MFT_STRING, IDM_STARTLIMIT, buf);
				}
				AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT,       "停止限制");
				AppendMenu(hMenu, MFT_STRING, IDM_SETPERCENT,      "设置限制资源的百分比");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_KERNELLIMIT,    "使用内核态调度器");
				if (limitMgr.limitEnabled) {
					CheckMenuItem(hMenu, IDM_STARTLIMIT, MF_CHECKED);
				} else {
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_CHECKED);
				}
				if (limitMgr.useKernelMode) {
					CheckMenuItem(hMenu, IDM_KERNELLIMIT, MF_CHECKED);
				}
			} else if (g_Mode == 1) {
				if (!traceMgr.lockEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,     "SGuard限制器 - 用户手动暂停");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,     "SGuard限制器 - 等待游戏运行");
				} else {
					if (traceMgr.lockPid == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 正在分析");
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
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
					}
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：线程追踪  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3,    "锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1,    "弱锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3RR,  "锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1RR,  "弱锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK,   "解除锁定");
				if (traceMgr.lockMode == 1 || traceMgr.lockMode == 3) {
					AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
					sprintf(buf, "设置时间切分（当前：%d）", traceMgr.lockRound);
					AppendMenu(hMenu, MFT_STRING, IDM_SETRRTIME, buf);
				}
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
					if (driver.driverReady) {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 等待游戏运行");
					} else {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 模式无效（驱动初始化失败）");
					}
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
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 请等待");
					} else {
						sprintf(buf, "SGuard限制器 - 已提交  [%u/%u]", finished, total);
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
					}
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：MemPatch V3  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_DOPATCH,       "自动");
				AppendMenu(hMenu, MF_GRAYED, IDM_UNDOPATCH,       "撤销修改");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH1,  "inline Ntdll!NtQueryVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH2,  "inline User32!GetAsyncKeyState");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH3,  "inline Ntdll!NtWaitForSingleObject");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH4,  "re-write Ntdll!NtDelayExecution");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				sprintf(buf, "设置延时（当前：%u/%u/%u/%u）", patchMgr.patchDelay[0], patchMgr.patchDelay[1], patchMgr.patchDelay[2], patchMgr.patchDelay[3]);
				AppendMenu(hMenu, drvMenuType, IDM_SETDELAY, buf);

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

			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuOthers, "其他选项");
			AppendMenu(hMenu, MFT_STRING, IDM_EXIT,            "退出");


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

			// general
			case IDM_ABOUT:
				ShowAbout();
				break;
			case IDM_EXIT:
				if (g_Mode == 0 && limitMgr.limitEnabled) {
					limitMgr.disable();
				} else if (g_Mode == 1 && traceMgr.lockEnabled) {
					traceMgr.disable();
				} else if (g_Mode == 2 && patchMgr.patchEnabled) {
					patchMgr.disable();
				}
				PostMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			
			// mode
			case IDM_MODE_HIJACK:
				traceMgr.disable();
				patchMgr.disable();
				limitMgr.enable();
				g_Mode = 0;
				configMgr.writeConfig();
				break;
			case IDM_MODE_TRACE:
				limitMgr.disable();
				patchMgr.disable();
				traceMgr.enable();
				g_Mode = 1;
				configMgr.writeConfig();
				break;
			case IDM_MODE_PATCH:
				limitMgr.disable();
				traceMgr.disable();
				patchMgr.enable();
				g_Mode = 2;
				configMgr.writeConfig();
				break;

			// limit
			case IDM_STARTLIMIT:
				limitMgr.enable();
				break;
			case IDM_STOPLIMIT:
				limitMgr.disable();
				break;
			case IDM_SETPERCENT:
				DialogBox(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETPCTDIALOG), hWnd, SetPctDlgProc);
				configMgr.writeConfig();
				break;
			case IDM_KERNELLIMIT:
				limitMgr.useKernelMode = !limitMgr.useKernelMode;
				configMgr.writeConfig();
				MessageBox(0, "切换该选项需要你重启限制器。", "注意", MB_OK);
				break;
			
			// lock
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
				
			// patch
			case IDM_SETDELAY:
				if (IDYES == MessageBox(0, "请依次设置以下开关的强制延时。\n如果不想设置某个选项，可以直接关掉对应的窗口。\n\nNtQueryVirtualMemory\nGetAsyncKeyState\nNtWaitForSingleObject\nNtDelayExecution\n", "信息", MB_YESNO)) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 0);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 1);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 2);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_SETDELAYDIALOG), hWnd, SetDelayDlgProc, 3);
					configMgr.writeConfig();
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				}
				break;
			case IDM_PATCHSWITCH1:
				patchMgr.patchSwitches.NtQueryVirtualMemory = !patchMgr.patchSwitches.NtQueryVirtualMemory;
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH2:
				patchMgr.patchSwitches.GetAsyncKeyState = !patchMgr.patchSwitches.GetAsyncKeyState;
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH3:
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					patchMgr.patchSwitches.NtWaitForSingleObject = false;
				} else {
					if (IDYES == MessageBox(0, "这是旧版增强模式，已知可能导致游戏异常。如果你出现“3009”，“96”，“lol掉线”问题，需要立即关闭该选项。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtWaitForSingleObject = true;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH4:
				if (patchMgr.patchSwitches.NtDelayExecution) {
					patchMgr.patchSwitches.NtDelayExecution = false;
				} else {
					if (IDYES == MessageBox(0, "这是旧版功能，不建议启用该选项，以免出现“3009”，“96”，“偶尔卡顿”等问题。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtDelayExecution = true;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;

			// more options
			case IDM_MORE_UPDATEPAGE:
				ShellExecute(0, "open", "https://bbs.colg.cn/thread-8087898-1-1.html", 0, 0, SW_SHOW);
				break;
			case IDM_MORE_SOURCEPAGE:
				ShellExecute(0, "open", "https://github.com/H3d9/sguard_limit", 0, 0, SW_SHOW);
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