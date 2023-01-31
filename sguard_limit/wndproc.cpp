#include <Windows.h>
#include <CommCtrl.h>
#include <stdio.h>
#include <atomic>
#include <random>
#include "wndproc.h"
#include "resource.h"
#include "kdriver.h"
#include "win32utility.h"
#include "config.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"

#pragma comment(lib, "Comctl32.lib")
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

extern std::atomic<bool>        g_HijackThreadWaiting;
extern std::atomic<DWORD>       g_Mode;



// about func: show about dialog box.
static void ShowAbout() {

	if (IDOK == MessageBox(0,
		"本工具专用于约束TX游戏扫盘插件ACE-Guard Client EXE占用CPU和扫盘。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"如果你发现无法正常使用，请参考附带的文档；若还不行请停止使用并等待更新。\n\n"

		"【使用方法】双击打开即可。若无报错则无需其他设置。\n\n"
		
		"【提示】 本工具是免费软件，任何出售本工具的人都是骗子哦！\n\n"
		"【提示】 不要使用工具强行关闭上述扫盘插件，这会导致游戏掉线！\n\n"
		"【提示】 使用中可能出现的大部分问题已经写在附带的文档里，请自行查看！\n\n\n"

		"SGUARD讨论群：775176979（满），877315766（满），637691678（满），\n"
		"            708641149，708413375\n"
		"更新链接/源代码：见右键菜单→其他选项。\n\n"
		"点击“确定”翻到下一页；点击“取消”结束查看说明。",
		"SGuard限制器 " VERSION "  by: @H3d9",
		MB_OKCANCEL)) {

		if (IDOK == MessageBox(0,
			"【工作模式说明 P1】\n\n"
			"内存补丁 " MEMPATCH_VERSION "（21.10.6）：\n\n"
			"这是默认模式，建议优先使用，如果不好用再换其他模式。\n\n"

			">1 NtQueryVirtualMemory(V2): 令SGUARD扫内存的速度变慢。\n"
			">2 NtReadVirtualMemory(V4.3): 拒绝SGUARD在应用层跨进程读内存。\n"
			">3 GetAsyncKeyState(V3): 令SGUARD读取键盘按键的速度变慢。\n"
			">4 NtWaitForSingleObject, NtDelayExecution: 已弃用。\n\n"
			">5 伪造ACE-BASE.sys的MDL控制代码(V4.2): 强化防扫盘，防止间歇性卡硬盘\n"
			">5 缓解指向ACE-BASE的CPL0通信速度(V4.6): 弱化防扫盘，避免安全组件运行异常\n"
			">6 执行失败的文件系统记录枚举(V4.2): 防止高强度扫硬盘（偶尔出现）\n"
			"【注】游戏刚启动时SG读盘是不可避免的，若屏蔽则游戏会启动失败。\n"
			" 间歇性卡硬盘原因为SG使用MDL读其他进程内存而这些内存刚好位于页面文件。\n\n"
			">7 [R0] 限制System进程占用CPU(V4.9)：更新为data_hijack。\n\n\n"
			
			"> 高级内存搜索(V4)：启用后修改内存可以瞬间完成。\n"
			"【注】你可以在“设置延迟”中更改“等待稳定的时间”（第二个，默认20秒那个）\n"
			" 来决定修改内存的时机。非常不建议将该数值调整的过小，以免游戏启动失败。\n"
			" 如果你游戏启动较慢，可以调高这一项，或先开游戏再开限制器。\n\n"

			"【说明】该模式需要临时装载一次驱动，修改内存后会立即卸载驱动。\n"
			" 若你使用时出现问题，可以去更新链接下载证书。\n\n\n"
			"点击“确定”翻到下一页；点击“取消”结束查看说明。",
			"SGuard限制器 " VERSION "  by: @H3d9",
			MB_OKCANCEL)) {

			if (IDOK == MessageBox(0,
				"【工作模式说明 P2】\n\n"
				"线程追踪（21.7.17）（已弃用）：\n\n"
				"该模式仅针对DNF，且仅推荐使用-rr后缀的功能。\n"
				"你可以点击“设置时间切分”并尝试诸如90，85，80...直到合适即可。\n\n"
				"【注】“时间切分”设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n"
				"【注】根据统计反馈，目前该模式中最好用的选项为【弱锁定-rr】。\n\n\n"
				"点击“确定”翻到下一页；点击“取消”结束查看说明。",
				"SGuard限制器 " VERSION "  by: @H3d9",
				MB_OKCANCEL)) {

				MessageBox(0,
					"【工作模式说明 P3】\n\n"
					"时间片轮转（21.2.6）（已弃用）：\n\n"
					"该模式原理与BES相同，不建议使用（可能出上报异常和重新连接）。\n\n"
					"【注】时间转轮无法限制扫硬盘，只能限制cpu使用。",
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
			
			if (dlgParam == DLGPARAM_RRPCT) { // set limit percent.
				SetWindowText(hDlg, "输入限制资源的百分比");
				sprintf(buf, "输入整数1~99，或999（代表99.9）\n（当前值：%u）", limitMgr.limitPercent.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_LOCKTIME) { // set time slice.
				SetWindowText(hDlg, "输入每100ms从目标线程中强制剥夺的时间（单位：ms）");
				sprintf(buf, "\n输入1~99的整数（当前值：%u）", traceMgr.lockRound.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_PATCHWAIT) { // set advanced patch wait for ntdll etc.
				SetWindowText(hDlg, "输入开启防扫盘后等待SGUARD稳定的时间（单位：秒）");
				sprintf(buf, "\n输入一个整数（当前等待时间：%u秒）", patchMgr.patchDelayBeforeNtdlletc.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);
				
			} else { // set patch delay switches.
				auto id = dlgParam - DLGPARAM_PATCHDELAY1;
				SetWindowText(hDlg, "输入SGUARD每次执行目标系统调用的强制延迟（单位：ms）");
				sprintf(buf, "\n输入%u~%u的整数（当前值：%u）", delayRange[id].low, delayRange[id].high, delay[id].load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

				if (dlgParam == DLGPARAM_PATCHDELAY1) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtQueryVirtualMemory");
				} else if (dlgParam == DLGPARAM_PATCHDELAY2) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：GetAsyncKeyState");
				} else if (dlgParam == DLGPARAM_PATCHDELAY3) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtWaitForSingleObject");
				} else if (dlgParam == DLGPARAM_PATCHDELAY4) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：NtDelayExecution");
				} else if (dlgParam == DLGPARAM_PATCHDELAY5) {
					SetDlgItemText(hDlg, IDC_TEXT2, "当前设置：指向ACE-BASE的CPL0通信时间");
				}
			}

			SetWindowPos(hDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

			return (INT_PTR)TRUE;
		}

		case WM_COMMAND:
		{
			if (LOWORD(wParam) == IDC_OK) {
				BOOL translated;
				UINT res = GetDlgItemInt(hDlg, IDC_EDIT, &translated, FALSE);
				
				if (dlgParam == DLGPARAM_RRPCT) {
					if (!translated || res < 1 || (res > 99 && res != 999)) {
						systemMgr.panic("输入1~99或999");
					} else {
						limitMgr.setPercent(res);
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_LOCKTIME) {
					if (!translated || res < 1 || res > 99) {
						systemMgr.panic("输入1~99的整数");
					} else {
						traceMgr.lockRound = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_PATCHWAIT) {
					if (!translated) {
						systemMgr.panic("输入格式错误");
					} else {
						patchMgr.patchDelayBeforeNtdlletc = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else {
					auto id = dlgParam - DLGPARAM_PATCHDELAY1;
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

static INT_PTR CALLBACK DlgProc2(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {

	case WM_INITDIALOG:
	{
		std::random_device rd;
		std::default_random_engine e(rd());
		
		const wchar_t* emoji[] = {
			L":.ﾟヽ(｡◕‿◕｡)ﾉﾟ.:｡+ﾟ",L"◔ ‸◔？",L"╮(๑•́ ₃•̀๑)╭",L"ฅʕ•̫͡•ʔฅ ",L"٩(๑´0`๑)۶",L"（//▽//）",L"Ծ‸Ծ",L"( ´◔ ‸◔`)",
			L"<(￣︶￣)>",L"(•‾̑⌣‾̑•)✧˖°",L"(๑˘ ˘๑) ",L"(¦3[▓▓]",L"✧ʕ̢̣̣̣̣̩̩̩̩·͡˔·ོɁ̡̣̣̣̣̩̩̩̩✧",L"*:ஐ٩(๑´ᵕ`)۶ஐ:*",L"(๑¯◡¯๑)",
			L"( *￣▽￣)o ─═≡※:☆",L"(～﹃～)~zZ",L"๐·°(৹˃̵﹏˂̵৹)°·๐",L"(๑✦ˑ̫✦)✨",L"ଘ(੭ˊᵕˋ)੭* ੈ✩‧₊˚",L"(●♡◡♡●)",
		};
		std::uniform_int_distribution<> u(0, sizeof(emoji) / sizeof(const wchar_t*) - 1);

		SetDlgItemTextW(hDlg, IDC_BONUS, emoji[u(e)]);
		SetWindowPos(hDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
	{
		if (LOWORD(wParam) == IDC_AFDIAN) {
			ShellExecute(0, "open", "https://afdian.net/a/sguard_limit", 0, 0, SW_SHOW);
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

	static UINT msg_taskbarRestart;

	switch (msg) {
	case WM_CREATE:
	{
		msg_taskbarRestart = RegisterWindowMessage("TaskbarCreated");
	}
	break;
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_LBUTTONUP ||
			lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {

			// for driver-depending options: 
			// auto select MFT_STRING or MF_GRAYED.
			auto    drvMenuType    = driver.driverReady ? MFT_STRING : MF_GRAYED;
			auto    drvPopMenuType = driver.driverReady ? MF_POPUP   : MF_GRAYED;


			char    buf[0x1000] = {};
			HMENU   hMenu       = CreatePopupMenu();
			HMENU   hMenuModes  = CreatePopupMenu();
			HMENU   hMenuOthers = CreatePopupMenu();


			AppendMenu(hMenuModes, MFT_STRING, IDM_MODE_HIJACK, "切换到：时间片轮转");
			AppendMenu(hMenuModes, MF_GRAYED,  IDM_MODE_TRACE,  "切换到：线程追踪");
			AppendMenu(hMenuModes, MFT_STRING, IDM_MODE_PATCH,  "切换到：内存补丁 " MEMPATCH_VERSION);
			if (g_Mode == 0) {
				CheckMenuItem(hMenuModes, IDM_MODE_HIJACK, MF_CHECKED);
			} else if (g_Mode == 1) {
				CheckMenuItem(hMenuModes, IDM_MODE_TRACE, MF_CHECKED);
			} else { // if g_Mode == 2
				CheckMenuItem(hMenuModes, IDM_MODE_PATCH, MF_CHECKED);
			}


			AppendMenu(hMenuOthers, MFT_STRING, IDM_AUTOSTARTUP,     "开机自启");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_AUTOCHECKUPDATE, "自动检查更新");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_KILLACELOADER,   "游戏启动60秒后，结束ace-loader");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_OPENPROFILEDIR,  "打开系统用户目录");
			AppendMenu(hMenuOthers, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_UPDATEPAGE, "检查更新【当前版本：" VERSION "】");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_ABOUT,           "查看说明");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_SOURCEPAGE, "查看源代码");
			if (systemMgr.autoStartup) {
				CheckMenuItem(hMenuOthers, IDM_AUTOSTARTUP, MF_CHECKED);
			}
			if (systemMgr.autoCheckUpdate) {
				CheckMenuItem(hMenuOthers, IDM_AUTOCHECKUPDATE, MF_CHECKED);
			}
			if (systemMgr.killAceLoader) {
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
					sprintf(buf, "限制资源：%u%%", limitMgr.limitPercent.load());
					AppendMenu(hMenu, MFT_STRING, IDM_STARTLIMIT, buf);
				}
				AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT,    "停止限制");
				AppendMenu(hMenu, MFT_STRING, IDM_SETPERCENT,   "设置限制资源的百分比");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_KERNELLIMIT, "使用内核态调度器");
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
								sprintf(buf + strlen(buf), "%x[%c] ", traceMgr.lockedThreads[i].tid.load(), traceMgr.lockedThreads[i].locked ? 'O' : 'X');
							}
							break;
						case 1:
							for (auto i = 0; i < 3; i++) {
								sprintf(buf + strlen(buf), "%x[..] ", traceMgr.lockedThreads[i].tid.load());
							}
							break;
						case 2:
							sprintf(buf + strlen(buf), "%x[%c] ", traceMgr.lockedThreads[0].tid.load(), traceMgr.lockedThreads[0].locked ? 'O' : 'X');
							break;
						case 3:
							sprintf(buf + strlen(buf), "%x[..] ", traceMgr.lockedThreads[0].tid.load());
							break;
						}
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
					}
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：线程追踪  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3,   "锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1,   "弱锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3RR, "锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1RR, "弱锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK,  "解除锁定");
				if (traceMgr.lockMode == 1 || traceMgr.lockMode == 3) {
					AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
					sprintf(buf, "设置时间切分（当前：%d）", traceMgr.lockRound.load());
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
					AppendMenuW(hMenu, MFT_STRING, IDM_ABOUT, L"SGuard限制器 - 模式无效（驱动初始化失败）");
				} else {
					if (g_HijackThreadWaiting) {
						AppendMenuW(hMenu, MFT_STRING, IDM_ABOUT, L"SGuard限制器 - 等待游戏运行");
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
							AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard限制器 - 请等待");
						} else {
							sprintf(buf, "SGuard限制器 - 已提交  [%d/%d]", finished, total);
							AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
						}
					}
				}

				HMENU hMenuPatch = CreatePopupMenu();
				AppendMenu(hMenuPatch, drvMenuType, IDM_PATCHSWITCH6_1, "切换到：防扫盘1弱化模式");
				AppendMenu(hMenuPatch, MF_GRAYED,   IDM_PATCHSWITCH6_2, "切换到：防扫盘1强力模式");
				AppendMenu(hMenuPatch, drvMenuType, IDM_PATCHSWITCH6_3, "关闭防扫盘1选项");
				if (patchMgr.patchSwitches.DeviceIoControl_1) {
					if (patchMgr.patchSwitches.DeviceIoControl_1x) {
						CheckMenuItem(hMenuPatch, IDM_PATCHSWITCH6_1, MF_CHECKED);
					} else {
						CheckMenuItem(hMenuPatch, IDM_PATCHSWITCH6_2, MF_CHECKED);
					}
				} else {
					CheckMenuItem(hMenuPatch, IDM_PATCHSWITCH6_3, MF_CHECKED);
				}

				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "当前模式：内存补丁 " MEMPATCH_VERSION "  [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_DOPATCH, "自动");
				AppendMenu(hMenu, MF_GRAYED, IDM_UNDOPATCH, "撤销修改");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH1, "inline Ntdll!NtQueryVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH2, "inline Ntdll!NtReadVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH3, "inline User32!GetAsyncKeyState");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH4, "inline Ntdll!NtWaitForSingleObject");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH5, "re-write Ntdll!NtDelayExecution");
				if (patchMgr.patchSwitches.DeviceIoControl_1x) {
					AppendMenu(hMenu, drvPopMenuType, (UINT_PTR)hMenuPatch, "[防扫盘1] 缓解指向ACE-BASE的CPL0通信速度");
				} else {
					AppendMenu(hMenu, drvPopMenuType, (UINT_PTR)hMenuPatch, "[防扫盘1] 伪造ACE-BASE.sys的MDL控制代码");
				}
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH7, "[防扫盘2] 执行失败的文件系统记录枚举");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH8, "[R0] 限制System进程占用CPU（data_hijack）");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_ADVMEMSEARCH, "启用高级内存搜索");
				sprintf(buf, "设置延迟（当前：0/%u", patchMgr.patchDelayBeforeNtdlletc.load());
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[0].load());
				}
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[1].load());
				}
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[2].load());
				}
				if (patchMgr.patchSwitches.NtDelayExecution) {
					sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[3].load());
				}
				if (patchMgr.patchSwitches.DeviceIoControl_1) {
					if (patchMgr.patchSwitches.DeviceIoControl_1x) {
						sprintf(buf + strlen(buf), "/%u", patchMgr.patchDelay[4].load());
					}
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
					CheckMenuItem(hMenu, (UINT)(UINT_PTR)hMenuPatch, MF_CHECKED);
				}
				if (patchMgr.patchSwitches.DeviceIoControl_2) {
					CheckMenuItem(hMenu, IDM_PATCHSWITCH7, MF_CHECKED);
				}
				CheckMenuItem(hMenu, IDM_ADVMEMSEARCH, MF_CHECKED);
			}

			AppendMenu(hMenu,  MF_SEPARATOR, 0, NULL);
			AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMenuOthers, L"查看说明/其他选项");
			AppendMenuW(hMenu, MFT_STRING, IDM_DONATE,          L"✨ 赞助一下，爱你哦~");
			AppendMenuW(hMenu, MFT_STRING, IDM_EXIT,            L"退出");


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
		char  buf[0x1000];
		UINT  id = LOWORD(wParam);

		switch (id) {

			// general
		case IDM_ABOUT:
			ShowAbout();
			break;
		case IDM_EXIT:
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				limitMgr.disable();
				g_HijackThreadWaiting.wait(false); // block till hijack release target.
			} else if (g_Mode == 1 && traceMgr.lockEnabled) {
				traceMgr.disable();
			} else if (g_Mode == 2 && patchMgr.patchEnabled) {
				patchMgr.disable();
			}
			PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;

			// mode
		case IDM_MODE_HIJACK:
			if (IDYES == MessageBox(0, "警告：该选项已废弃！\n\n仅在“内存补丁”模式无法使用时，才能使用该选项。要继续么？", "警告：功能已弃用！", MB_YESNO)) {
				traceMgr.disable();
				patchMgr.disable();
				limitMgr.enable();
				g_Mode = 0;
				configMgr.writeConfig();
			}
			break;
		case IDM_MODE_TRACE:
			/*if (IDYES == MessageBox(0, "警告：该选项已废弃！\n\n不要启用这一选项，除非你知道你在做什么，要继续么？", "警告：功能已弃用！", MB_YESNO)) {
				limitMgr.disable();
				patchMgr.disable();
				traceMgr.enable();
				g_Mode = 1;
				configMgr.writeConfig();
			}*/
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
			DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), hWnd, DlgProc, DLGPARAM_RRPCT);
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
			DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), hWnd, DlgProc, DLGPARAM_LOCKTIME);
			configMgr.writeConfig();
			break;
		case IDM_UNLOCK:
			traceMgr.disable();
			break;

			// patch
		case IDM_SETDELAY:
		{
			TASKDIALOG_BUTTON buttons[] = {
			 { 1000, L"开启防扫盘功能后等待SGUARD稳定的时间" },
			 { 1001, L"NtQueryVirtualMemory" },
			 { 1002, L"GetAsyncKeyState" },
			 { 1003, L"NtWaitForSingleObject" },
			 { 1004, L"NtDelayExecution" },
			 { 1005, L"指向ACE-BASE的CPL0通信时间" },
			};

			TASKDIALOGCONFIG config    = { sizeof(TASKDIALOGCONFIG) };
			config.hwndParent          = hWnd;
			config.dwFlags             = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
			config.dwCommonButtons     = TDCBF_CANCEL_BUTTON;
			config.pButtons            = buttons;
			config.cButtons            = _countof(buttons);
			config.nDefaultButton      = 1005;
			config.pszWindowTitle      = L"设置延迟";
			config.pszMainIcon         = TD_INFORMATION_ICON;
			config.pszMainInstruction  = L"选定一个选项以开始设置延迟。";
			config.pszContent          = L"如果不知道某选项的作用，请勿胡乱设置，否则可能游戏掉线！\n设置的延迟必须在相应右键菜单显示为已选择时才生效。";

			int buttonClicked;
			if (SUCCEEDED(TaskDialogIndirect(&config, &buttonClicked, NULL, NULL)) && buttonClicked != IDCANCEL) {
				
				if (buttonClicked == 1000) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHWAIT);
				} else if (buttonClicked == 1001) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHDELAY1);
				} else if (buttonClicked == 1002) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHDELAY2);
				} else if (buttonClicked == 1003) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHDELAY3);
				} else if (buttonClicked == 1004) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHDELAY4);
				} else if (buttonClicked == 1005) {
					DialogBoxParam(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, DlgProc, DLGPARAM_PATCHDELAY5);
				}

				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
			}
		}
		break;
		case IDM_PATCHSWITCH1:
			if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
				if (IDYES == MessageBox(0, "点击“是”将关闭NtQueryVirtualMemory开关。\n若你不知道如何选择，请回答“否”。", "注意", MB_YESNO)) {
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
				if (IDYES == MessageBox(0, "点击“是”将关闭NtReadVirtualMemory开关。\n若你不知道如何选择，请回答“否”。", "注意", MB_YESNO)) {
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
				if (IDYES == MessageBox(0, "点击“是”将关闭GetAsyncKeyState开关。\n若你不知道如何选择，请回答“否”。", "注意", MB_YESNO)) {
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
				if (IDYES == MessageBox(0, "警告：如使用未出现问题或未说明，请勿勾选该选项。\n\n仍要继续吗？", "警告", MB_YESNO)) {
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
				if (IDYES == MessageBox(0, "警告：该选项已废弃！\n\n不要启用这一选项，除非你知道你在做什么，要继续么？", "警告：功能已弃用！", MB_YESNO)) {
					patchMgr.patchSwitches.NtDelayExecution = true;
				} else {
					break;
				}
			}
			configMgr.writeConfig();
			MessageBox(0, "重启游戏后生效", "注意", MB_OK);
			break;
		case IDM_PATCHSWITCH6_1: // weak ioctl_1
			if (!patchMgr.patchSwitches.DeviceIoControl_1x) {
				patchMgr.patchSwitches.DeviceIoControl_1 = true;
				patchMgr.patchSwitches.DeviceIoControl_1x = true;

				configMgr.writeConfig();
				MessageBox(0, "重启游戏后生效", "注意", MB_OK);
			}
			break;
		case IDM_PATCHSWITCH6_2: // strong ioctl_1
			/*if (!(patchMgr.patchSwitches.DeviceIoControl_1 && !patchMgr.patchSwitches.DeviceIoControl_1x)) {
				if (IDYES == MessageBox(0, "警告：该选项已废弃！\n\n不要启用这一选项，除非你知道你在做什么，要继续么？", "警告：功能已弃用！", MB_YESNO)) {
					patchMgr.patchSwitches.DeviceIoControl_1 = true;
					patchMgr.patchSwitches.DeviceIoControl_1x = false;

					configMgr.writeConfig();
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				}
			}*/
			break;
		case IDM_PATCHSWITCH6_3: // close ioctl_1
			if (patchMgr.patchSwitches.DeviceIoControl_1) {
				if (IDYES == MessageBox(0, "点击“是”将关闭[防扫盘1]开关。\n若你不知道如何选择，请回答“否”。", "注意", MB_YESNO)) {
					patchMgr.patchSwitches.DeviceIoControl_1 = false;
					patchMgr.patchSwitches.DeviceIoControl_1x = false;

					configMgr.writeConfig();
					MessageBox(0, "重启游戏后生效", "注意", MB_OK);
				}
			}
			break;
		case IDM_PATCHSWITCH7:
			if (patchMgr.patchSwitches.DeviceIoControl_2) {
				if (IDYES == MessageBox(0, "点击“是”将关闭[防扫盘2]开关。\n若你不知道如何选择，请回答“否”。", "注意", MB_YESNO)) {
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
		case IDM_PATCHSWITCH8:
		{
			TASKDIALOG_BUTTON buttons[] = {
			 { 1000, L"我已阅读并继续(&R)" },
			 { 1001, L"取消(&C)" },
			};

			TASKDIALOGCONFIG config    = { sizeof(TASKDIALOGCONFIG) };
			config.hwndParent          = hWnd;
			config.dwFlags             = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
			config.pButtons            = buttons;
			config.cButtons            = _countof(buttons);
			config.nDefaultButton      = 1001;
			config.pszWindowTitle      = L"Ring 0 操作警告";
			config.pszMainIcon         = TD_WARNING_ICON;
			config.pszMainInstruction  = L"点击“继续”将限制system进程占用cpu。";
			config.pszContent          = L"必须先开游戏再点该选项。\n你发现“System”持续占用很高CPU时使用，\n否则可能出现错误！";

			int buttonClicked;
			if (SUCCEEDED(TaskDialogIndirect(&config, &buttonClicked, NULL, NULL)) && buttonClicked == 1000) {
				if (patchMgr.patch_r0()) {
					MessageBox(0, "data_hijack: 操作成功，请自行观察System进程的cpu占用。", "提示", MB_OK);
				}
			}
		}
		break;
			// more options
		case IDM_AUTOSTARTUP:
		{
			sprintf(buf, "点击“是”将%s限制器开机自启。", systemMgr.autoStartup ? "禁用" : "启用");
			if (IDYES == MessageBox(0, buf, "提示", MB_YESNO)) {

				// set flag and modify registry. set back if modify fail.
				systemMgr.autoStartup = !systemMgr.autoStartup;

				if (!systemMgr.modifyStartupReg()) {
					systemMgr.autoStartup = !systemMgr.autoStartup;
				}

				configMgr.writeConfig();
			}
		}
		break;
		case IDM_AUTOCHECKUPDATE:
		{
			sprintf(buf, "点击“是”将%s自动检查更新。", systemMgr.autoCheckUpdate ? "禁用" : "启用");
			if (IDYES == MessageBox(0, buf, "提示", MB_YESNO)) {

				systemMgr.autoCheckUpdate = !systemMgr.autoCheckUpdate;
				configMgr.writeConfig();
			}
		}
		break;
		case IDM_KILLACELOADER:
		{
			sprintf(buf, "点击“是”将%s自动结束ace-loader功能。", systemMgr.killAceLoader ? "禁用" : "启用");
			if (IDYES == MessageBox(0, buf, "提示", MB_YESNO)) {

				systemMgr.killAceLoader = !systemMgr.killAceLoader;
				configMgr.writeConfig();
			}
		}
		break;
		case IDM_OPENPROFILEDIR:
			ShellExecute(0, "open", systemMgr.getProfileDir().c_str(), 0, 0, SW_SHOW);
			break;
		case IDM_MORE_UPDATEPAGE:
			ShellExecute(0, "open", "https://space.bilibili.com/1795170/article", 0, 0, SW_SHOW);
			break;
		case IDM_MORE_SOURCEPAGE:
			ShellExecute(0, "open", "https://github.com/H3d9/sguard_limit", 0, 0, SW_SHOW);
			break;
		case IDM_DONATE:
			DialogBox(systemMgr.hInstance, MAKEINTRESOURCE(IDD_DONATE), NULL, DlgProc2);
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
	default:
	{
		if (msg == msg_taskbarRestart) {
			systemMgr.createTray(WM_TRAYACTIVATE);
		}
	}
	break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}