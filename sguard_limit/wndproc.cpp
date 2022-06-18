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

#pragma comment(linker, "/manifestdependency:\"type='win32' \
						 name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
						 processorArchitecture='*' publicKeyToken='6595b64144ccf1df' \
						 language='*'\"")


extern KernelDriver&            driver;
extern win32SystemManager&      systemMgr;
extern ConfigManager&           configMgr;
extern LimitManager&            limitMgr;
extern TraceManager&            traceMgr;
extern PatchManager&            patchMgr;

extern volatile bool            g_HijackThreadWaiting;
extern volatile DWORD           g_Mode;

extern volatile bool            g_KillAceLoader;


// about func: show about dialog box.
static void ShowAbout() {

	if (IDOK == MessageBox(0,
		"本工具专用于约束TX游戏后台扫盘插件ACE-Guard Client EXE占用系统资源。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"如果你发现无法正常使用，请更换模式或选项；若还不行请停止使用并等待更新。\n\n"

		"【使用方法】双击打开，右下角出现托盘即可。若无报错则无需其他设置。\n\n\n"

		"【提示】 不要强行关闭上述扫盘插件，这会导致游戏掉线。\n\n"
		"【提示】 本工具是免费软件，任何出售本工具的人都是骗子哦！\n\n\n"

		"SGUARD讨论群：775176979\n"
		"更新链接/源代码：见右键菜单→其他选项。\n\n"
		"点击“确定”翻到下一页；点击“取消”结束查看说明。",
		"SGuard限制器 " VERSION "  by: @H3d9",
		MB_OKCANCEL)) {

		if (IDOK == MessageBox(0,
			"【工作模式说明 P1】\n\n"
			"内存补丁 " MEMPATCH_VERSION "（21.10.6）：\n\n"
			"这是默认模式，建议优先使用，如果不好用再换其他模式。\n\n"

			">1 NtQueryVirtualMemory(V2新增): 令SGUARD扫内存的速度变慢。\n\n"
			">1 NtReadVirtualMemory(V4.3新增): 拒绝SGUARD在应用层跨进程读内存。\n\n"
			">2 GetAsyncKeyState(V3新增): 令SGUARD读取键盘按键的速度变慢。\n"
			"【注】启用该选项似乎可以提升流畅度。相关的引用位于动态库ACE-DRV64.dll中。\n\n"

			">3 NtWaitForSingleObject: 旧功能，不建议使用：已知可能导致游戏异常。\n"
			">4 NtDelayExecution: 旧功能，不建议使用：已知可能导致游戏异常和卡顿。\n\n"

			">5 伪造ACE-BASE.sys的MDL控制代码(V4.2新增): 防止间歇性卡硬盘（经常出现）。\n"
			">6 执行失败的文件系统记录枚举(V4.2新增): 防止高强度扫硬盘（偶尔出现）。\n"
			"【注】游戏刚启动时SG读盘是不可避免的，若屏蔽则游戏会启动失败。\n"
			"【注】间歇性卡硬盘原因为SG使用MDL读其他进程内存而这些内存刚好位于页面文件。\n\n\n"
			
			"> 高级内存搜索(V4新增)：该功能用于解决无法定位模块User32。\n"
			"【注】启用该功能后不再需要采样指令指针，故修改内存可以瞬间完成。\n"
			"【注】你可以在“设置延迟”中更改“等待SG稳定的时间”来决定修改内存的时机。\n"
			"     等待时间越大，则游戏启动时越不易掉线。姥爷机可以多设置几十秒。\n"
			"     等待时间越小，则限制SG越快；设为0时可以启动游戏秒限制。\n\n"

			"【说明】该模式需要临时装载一次驱动，修改内存后会立即卸载驱动。\n"
			"若你使用时出现问题，可以去更新链接下载证书。\n\n\n"
			"点击“确定”翻到下一页；点击“取消”结束查看说明。",
			"SGuard限制器 " VERSION "  by: @H3d9",
			MB_OKCANCEL)) {

			if (IDOK == MessageBox(0,
				"【工作模式说明 P2】\n\n"
				"线程追踪（21.7.17）：\n\n"
				"该模式仅针对DNF，且仅推荐使用-rr后缀的功能。\n"
				"你可以点击“设置时间切分”并尝试诸如90，85，80...直到合适即可。\n\n"
				"【注】“时间切分”设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n"
				"【注】根据统计反馈，目前该模式中最好用的选项为【弱锁定-rr】。\n\n\n"
				"点击“确定”翻到下一页；点击“取消”结束查看说明。",
				"SGuard限制器 " VERSION "  by: @H3d9",
				MB_OKCANCEL)) {

				MessageBox(0,
					"【工作模式说明 P3】\n\n"
					"时间片轮转（21.2.6）：\n\n"
					"该模式原理与BES相同，不建议DNF使用（但是LOL可以用，不过有重新连接的风险）。\n\n"
					"【注1】如果LOL经常掉线连不上，可以切换到这个模式；且【不要打开】内核态调度器。\n"
					"【注2】对于DNF，如果你仍然想用这个模式，建议打开内核态调度器。\n"
					"【注3】时间转轮可能无法约束扫硬盘。",
					"SGuard限制器 " VERSION "  by: @H3d9",
					MB_OK);
			}
		}
	}
}


static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	static DWORD dlgParam = 0;

	auto& delayRange = patchMgr.patchDelayRange;
	auto& delay      = patchMgr.patchDelay;
	
	switch (message) {

		case WM_INITDIALOG:
		{
			char buf [0x1000];
			dlgParam = (DWORD)lParam;
			
			if (dlgParam == DLGPARAM_PCT) { // set limit percent.
				SetWindowText(hDlg, "输入限制资源的百分比");
				sprintf(buf, "输入整数1~99，或999（代表99.9）\n（当前值：%u）", limitMgr.limitPercent);
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_TIME) { // set time slice.
				SetWindowText(hDlg, "输入每100ms从目标线程中强制剥夺的时间（单位：ms）");
				sprintf(buf, "\n输入1~99的整数（当前值：%u）", traceMgr.lockRound);
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_PATCHWAIT1) { // set advanced patch wait for ntdll ioctl.
				SetWindowText(hDlg, "输入开启防扫盘功能前的等待时间（单位：秒）");
				sprintf(buf, "\n输入一个整数（当前等待时间：%u秒）", patchMgr.patchDelayBeforeNtdllioctl);
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_PATCHWAIT2) { // set advanced patch wait for ntdll etc.
				SetWindowText(hDlg, "输入开启防扫盘功能后等待SGUARD稳定的时间（单位：秒）");
				sprintf(buf, "\n输入一个整数（当前等待时间：%u秒）", patchMgr.patchDelayBeforeNtdlletc);
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else { // set patch delay switches.
				auto id = dlgParam - DLGPARAM_DELAY1;
				SetWindowText(hDlg, "输入SGUARD每次执行当前系统调用的强制延迟（单位：ms）");
				sprintf(buf, "\n输入%u~%u的整数（当前值：%u）", delayRange[id].low, delayRange[id].high, delay[id]);
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

				if (dlgParam == DLGPARAM_DELAY1) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtQueryVirtualMemory");
				} else if (dlgParam == DLGPARAM_DELAY2) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：GetAsyncKeyState");
				} else if (dlgParam == DLGPARAM_DELAY3) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtWaitForSingleObject\n【注意】不建议设置大于100的数值。");
				} else if (dlgParam == DLGPARAM_DELAY4) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtDelayExecution");
				}
			}

			return (INT_PTR)TRUE;
		}

		case WM_COMMAND:
		{
			if (LOWORD(wParam) == IDC_OK) {
				BOOL translated;
				UINT res = GetDlgItemInt(hDlg, IDC_EDIT, &translated, FALSE);

				if (dlgParam == DLGPARAM_PCT) {
					if (!translated || res < 1 || (res > 99 && res != 999)) {
						systemMgr.panic("输入1~99或999");
					} else {
						limitMgr.setPercent(res);
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_TIME) {
					if (!translated || res < 1 || res > 99) {
						systemMgr.panic("输入1~99的整数");
					} else {
						traceMgr.lockRound = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_PATCHWAIT1) {
					if (!translated) {
						systemMgr.panic("输入格式错误");
					} else {
						patchMgr.patchDelayBeforeNtdllioctl = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_PATCHWAIT2) {
					if (!translated) {
						systemMgr.panic("输入格式错误");
					} else {
						patchMgr.patchDelayBeforeNtdlletc = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else {
					auto id = dlgParam - DLGPARAM_DELAY1;
					if (!translated || res < delayRange[id].low || res > delayRange[id].high) {
						systemMgr.panic("输入%u~%u的整数", delayRange[id].low, delayRange[id].high);
					} else {
						patchMgr.patchDelay[id] = res;
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
		break;
	}

	return (INT_PTR)FALSE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	switch (msg) {
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_LBUTTONUP ||
			lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {

			// for driver-depending options: 
			// auto select MFT_STRING or MF_GRAYED.
			auto    drvMenuType    = driver.driverReady ? MFT_STRING : MF_GRAYED;


			CHAR    buf   [0x1000] = {};
			HMENU   hMenu          = CreatePopupMenu();
			HMENU   hMenuModes     = CreatePopupMenu();
			HMENU   hMenuOthers    = CreatePopupMenu();

			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_HIJACK,  "切换到：时间片轮转");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_TRACE,   "切换到：线程追踪");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_PATCH,   "切换到：内存补丁 " MEMPATCH_VERSION);
			if (g_Mode == 0) {
				CheckMenuItem(hMenuModes, IDM_MODE_HIJACK, MF_CHECKED);
			} else if (g_Mode == 1) {
				CheckMenuItem(hMenuModes, IDM_MODE_TRACE,  MF_CHECKED);
			} else { // if g_Mode == 2
				CheckMenuItem(hMenuModes, IDM_MODE_PATCH,  MF_CHECKED);
			}

			AppendMenu(hMenuOthers, MFT_STRING, IDM_KILLACELOADER,   "游戏启动60秒后，结束ace-loader");
			AppendMenu(hMenuOthers, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_UPDATEPAGE, "检查更新【当前版本：" VERSION "】");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_ABOUT,           "查看说明");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_SOURCEPAGE, "查看源代码");
			if (g_KillAceLoader) {
				CheckMenuItem(hMenuOthers, IDM_KILLACELOADER, MF_CHECKED);
			}


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

			} else { // if (g_Mode == 2) 

				if (!driver.driverReady) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,      "SGuard限制器 - 模式无效（驱动未初始化）");
				} else {
					if (g_HijackThreadWaiting) {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,  "SGuard限制器 - 等待游戏运行");
					} else {
						int total = 
							patchMgr.patchSwitches.NtQueryVirtualMemory + 
							patchMgr.patchSwitches.NtReadVirtualMemory + 
							patchMgr.patchSwitches.GetAsyncKeyState + 
							patchMgr.patchSwitches.NtWaitForSingleObject + 
							patchMgr.patchSwitches.NtDelayExecution + 
							patchMgr.patchSwitches.DeviceIoControl_1 + 
							patchMgr.patchSwitches.DeviceIoControl_2;

						int finished = 
							patchMgr.patchStatus.NtQueryVirtualMemory + 
							patchMgr.patchStatus.NtReadVirtualMemory + 
							patchMgr.patchStatus.GetAsyncKeyState + 
							patchMgr.patchStatus.NtWaitForSingleObject + 
							patchMgr.patchStatus.NtDelayExecution + 
							patchMgr.patchStatus.DeviceIoControl_1 + 
							patchMgr.patchStatus.DeviceIoControl_2;

						if (finished == 0) {
							if (patchMgr.patchFailCount == 0) {
								AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 请等待");
							} else {
								sprintf(buf, "SGuard限制器 - 正在重试（第%d次）... << [点击查看详细信息]", patchMgr.patchFailCount);
								AppendMenu(hMenu, MFT_STRING, IDM_PATCHFAILHINT, buf);
							}
						} else {
							sprintf(buf, "SGuard限制器 - 已提交  [%d/%d]", finished, total);
							AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
						}
					}
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：内存补丁 " MEMPATCH_VERSION "  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_DOPATCH,       "自动");
				AppendMenu(hMenu, MF_GRAYED, IDM_UNDOPATCH,       "撤销修改");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH1,  "inline Ntdll!NtQueryVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH2,  "inline Ntdll!NtReadVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH3,  "inline User32!GetAsyncKeyState");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH4,  "inline Ntdll!NtWaitForSingleObject");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH5,  "re-write Ntdll!NtDelayExecution");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH6,  "[防扫盘1] 伪造ACE-BASE.sys的MDL控制代码");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH7,  "[防扫盘2] 执行失败的文件系统记录枚举");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_ADVMEMSEARCH, "启用高级内存搜索");
				sprintf(buf, "设置延迟（当前：%u/%u", patchMgr.patchDelayBeforeNtdllioctl, patchMgr.patchDelayBeforeNtdlletc);
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[0]);
				}
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[1]);
				}
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[2]);
				}
				if (patchMgr.patchSwitches.NtDelayExecution) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[3]);
				}
				strcat(buf, "）");
				AppendMenu(hMenu, drvMenuType, IDM_SETDELAY, buf);

				CheckMenuItem(hMenu, IDM_DOPATCH, MF_CHECKED);
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH1, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.NtReadVirtualMemory) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH2, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH3, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH4, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.NtDelayExecution) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH5, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.DeviceIoControl_1) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH6, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.DeviceIoControl_2) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH7, MF_CHECKED);
				}
				if (patchMgr.useAdvancedSearch) {
					CheckMenuItem(hMenu, IDM_ADVMEMSEARCH, MF_CHECKED);
				}
			}

			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuOthers, "查看说明/其他选项");
			AppendMenu(hMenu, MFT_STRING, IDM_EXIT,            "退出");


			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);
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
					while (!g_HijackThreadWaiting) {
						Sleep(1); // spin; wait till hijack release target.
					}
				} else if (g_Mode == 1 && traceMgr.lockEnabled) {
					traceMgr.disable();
				} else if (g_Mode == 2 && patchMgr.patchEnabled) {
					patchMgr.disable();
				}
				PostMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			
			// mode
			case IDM_MODE_HIJACK:
				if (IDYES == MessageBox(0, "“时间片轮转”是旧版功能，可能导致游戏掉线，建议默认模式无法使用时再换。你确定要切换吗？", "注意", MB_YESNO)) {
					traceMgr.disable();
					patchMgr.disable();
					limitMgr.enable();
					g_Mode = 0;
					configMgr.writeConfig();
				}
				break;
			case IDM_MODE_TRACE:
				if (IDYES == MessageBox(0, "“线程追踪”是旧版功能，可能导致游戏掉线，不建议使用。你确定要切换吗？", "注意", MB_YESNO)) {
					limitMgr.disable();
					patchMgr.disable();
					traceMgr.enable();
					g_Mode = 1;
					configMgr.writeConfig();
				}
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
				DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), hWnd, DlgProc, DLGPARAM_PCT);
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
				DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), hWnd, DlgProc, DLGPARAM_TIME);
				configMgr.writeConfig();
				break;
			case IDM_UNLOCK:
				traceMgr.disable();
				break;
				
			// patch
			case IDM_SETDELAY:
			{
				char buf[0x1000];
				sprintf(buf, "请依次设置以下选项的延迟。\n"
					"如果不知道某选项的含义或不想设置某选项，可以直接关掉对应的窗口。\n\n"
					"(高级内存搜索) 开启防扫盘功能前的等待时间\n"
					"(高级内存搜索) 开启防扫盘功能后等待SGUARD稳定的时间\n"
					"%s%s%s%s",
					patchMgr.patchSwitches.NtQueryVirtualMemory   ? "NtQueryVirtualMemory\n"   : "",
					patchMgr.patchSwitches.GetAsyncKeyState       ? "GetAsyncKeyState\n"       : "",
					patchMgr.patchSwitches.NtWaitForSingleObject  ? "NtWaitForSingleObject\n"  : "",
					patchMgr.patchSwitches.NtDelayExecution       ? "NtDelayExecution\n"       : "");

				if (IDYES == MessageBox(0, buf, "信息", MB_YESNO)) {

					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHWAIT1);
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHWAIT2);
					if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
						DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_DELAY1);
					}
					if (patchMgr.patchSwitches.GetAsyncKeyState) {
						DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_DELAY2);
					}
					if (patchMgr.patchSwitches.NtWaitForSingleObject) {
						DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_DELAY3);
					}
					if (patchMgr.patchSwitches.NtDelayExecution) {
						DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_DELAY4);
					}

					configMgr.writeConfig();
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				}
			}
				break;
			case IDM_PATCHSWITCH1:
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					if (IDYES == MessageBox(0, "点击“是”将关闭NtQueryVirtualMemory开关。", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtQueryVirtualMemory = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.NtQueryVirtualMemory = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH2:
				if (patchMgr.patchSwitches.NtReadVirtualMemory) {
					if (IDYES == MessageBox(0, "点击“是”将关闭NtReadVirtualMemory开关。", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtReadVirtualMemory = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.NtReadVirtualMemory = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH3:
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					if (IDYES == MessageBox(0, "点击“是”将关闭GetAsyncKeyState开关。", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.GetAsyncKeyState = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.GetAsyncKeyState = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH4:
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					patchMgr.patchSwitches.NtWaitForSingleObject = false;
				} else {
					if (IDYES == MessageBox(0, "这是旧版增强模式，已知可能导致游戏异常。如果你出现“3009”，“96”，“lol掉线”问题，需要立即关闭该选项。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtWaitForSingleObject = true;
					} else {
						break;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH5:
				if (patchMgr.patchSwitches.NtDelayExecution) {
					patchMgr.patchSwitches.NtDelayExecution = false;
				} else {
					if (IDYES == MessageBox(0, "这是旧版功能，不建议启用该选项，以免出现“3009”，“96”，“偶尔卡顿”等问题。要继续么？", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.NtDelayExecution = true;
					} else {
						break;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH6:
				if (patchMgr.patchSwitches.DeviceIoControl_1) {
					if (IDYES == MessageBox(0, "点击“是”将关闭NtDeviceIoControlFile开关。", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.DeviceIoControl_1 = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.DeviceIoControl_1 = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHSWITCH7:
				if (patchMgr.patchSwitches.DeviceIoControl_2) {
					if (IDYES == MessageBox(0, "点击“是”将关闭NtFsControlFile开关。", "注意", MB_YESNO)) {
						patchMgr.patchSwitches.DeviceIoControl_2 = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.DeviceIoControl_2 = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;
			case IDM_PATCHFAILHINT:
				MessageBox(0, 
					"出现“正在重试”字样表示限制器无法采样到SGUARD扫内存的指令。\n"
					"这一般由于SGUARD在最近的时间并没有扫内存导致。\n"
					"建议的解决方法：\n\n"
					"1 打开高级内存搜索功能。\n"
					"2 直接等待限制器自动重试。\n"
					"3 重启游戏或重启电脑。\n"
					"4 过一会儿再使用限制器。\n"
					"5 偶尔出现是正常的。但若每次启动游戏都出现且均长时间重试无效，应停止使用限制器"
					, "信息", MB_OK);
				break;
			case IDM_ADVMEMSEARCH:
				if (patchMgr.useAdvancedSearch) {
					if (IDYES == MessageBox(0, "点击“是”将关闭高级内存搜索功能。", "注意", MB_YESNO)) {
						patchMgr.useAdvancedSearch = false;
					} else {
						break;
					}
				} else {
					patchMgr.useAdvancedSearch = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				break;

			// more options
			case IDM_KILLACELOADER:
				if (g_KillAceLoader) {
					if (IDYES == MessageBox(0, "点击“是”将关闭自动结束ace-loader功能。", "注意", MB_YESNO)) {
						g_KillAceLoader = false;
					} else {
						break;
					}
				} else {
					g_KillAceLoader = true;
				}
				configMgr.writeConfig();
				break;
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