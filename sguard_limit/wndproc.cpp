#include <Windows.h>
#include <stdio.h>
#include <atomic>
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

extern std::atomic<bool>        g_HijackThreadWaiting;
extern std::atomic<DWORD>       g_Mode;



// about func: show about dialog box.
static void ShowAbout() {

	if (IDOK == MessageBox(0,
		"������ר����Լ��TX��Ϸ��̨ɨ�̲��ACE-Guard Client EXEռ��CPU��ɨ�̡�\n"
		"�ù��߽����о�������Ϸ�Ż�ʹ�ã���������ʧЧ������֤�ȶ��ԡ�\n"
		"����㷢���޷�����ʹ�ã������ģʽ��ѡ�����������ֹͣʹ�ò��ȴ����¡�\n\n"

		"��ʹ�÷�����˫���򿪼��ɡ����ޱ����������������á�\n\n\n"
		
		"����ʾ�� ��Ҫʹ�ù���ǿ�йر�����ɨ�̲������ᵼ����Ϸ���ߡ�\n\n"
		"����ʾ�� �����������������κγ��۱����ߵ��˶���ƭ��Ŷ��\n\n\n"

		"SGUARD����Ⱥ��775176979\n"
		"��������/Դ���룺���Ҽ��˵�������ѡ�\n\n"
		"�����ȷ����������һҳ�������ȡ���������鿴˵����",
		"SGuard������ " VERSION "  by: @H3d9",
		MB_OKCANCEL)) {

		if (IDOK == MessageBox(0,
			"������ģʽ˵�� P1��\n\n"
			"�ڴ油�� " MEMPATCH_VERSION "��21.10.6����\n\n"
			"����Ĭ��ģʽ����������ʹ�ã�����������ٻ�����ģʽ��\n\n"

			">1 NtQueryVirtualMemory(V2): ��SGUARDɨ�ڴ���ٶȱ�����\n\n"
			">2 NtReadVirtualMemory(V4.3): �ܾ�SGUARD��Ӧ�ò����̶��ڴ档\n\n"
			">3 GetAsyncKeyState(V3): ��SGUARD��ȡ���̰������ٶȱ�����\n\n"
			">4 NtWaitForSingleObject, NtDelayExecution: �����ã���Ҫʹ�á�\n\n"
			">5 α��ACE-BASE.sys��MDL���ƴ���(V4.2): ��ֹ��Ъ�Կ�Ӳ�̣��������֣���\n"
			">6 ִ��ʧ�ܵ��ļ�ϵͳ��¼ö��(V4.2): ��ֹ��ǿ��ɨӲ�̣�ż�����֣���\n"
			"��ע����Ϸ������ʱSG�����ǲ��ɱ���ģ�����������Ϸ������ʧ�ܡ�\n"
			" ��Ъ�Կ�Ӳ��ԭ��ΪSGʹ��MDL�����������ڴ����Щ�ڴ�պ�λ��ҳ���ļ���\n\n\n"
			
			"> �߼��ڴ�����(V4)�����ú��޸��ڴ����˲����ɡ�\n"
			"��ע��������ڡ������ӳ١��и��ġ��ȴ�SG�ȶ���ʱ�䡱���ڶ�����Ĭ��20���Ǹ���\n"
			" �������޸��ڴ��ʱ�����ǳ������齫����ֵ�����Ĺ�С��������Ϸ����ʧ�ܡ�\n"
			" �������Ϸ�������������Ե�����һ����ȿ���Ϸ�ٿ���������\n\n"

			"��˵������ģʽ��Ҫ��ʱװ��һ���������޸��ڴ�������ж��������\n"
			" ����ʹ��ʱ�������⣬����ȥ������������֤�顣\n\n\n"
			"�����ȷ����������һҳ�������ȡ���������鿴˵����",
			"SGuard������ " VERSION "  by: @H3d9",
			MB_OKCANCEL)) {

			if (IDOK == MessageBox(0,
				"������ģʽ˵�� P2��\n\n"
				"�߳�׷�٣�21.7.17����\n\n"
				"��ģʽ�����DNF���ҽ��Ƽ�ʹ��-rr��׺�Ĺ��ܡ�\n"
				"����Ե��������ʱ���з֡�����������90��85��80...ֱ�����ʼ��ɡ�\n\n"
				"��ע����ʱ���з֡����õ�ֵԽ����Լ���ȼ�Խ�ߣ����õ�ֵԽС����Խ�ȶ���\n"
				"��ע������ͳ�Ʒ�����Ŀǰ��ģʽ������õ�ѡ��Ϊ��������-rr����\n\n\n"
				"�����ȷ����������һҳ�������ȡ���������鿴˵����",
				"SGuard������ " VERSION "  by: @H3d9",
				MB_OKCANCEL)) {

				MessageBox(0,
					"������ģʽ˵�� P3��\n\n"
					"ʱ��Ƭ��ת��21.2.6����\n\n"
					"��ģʽԭ����BES��ͬ��������DNF��LOLʹ�ã����ܳ��ϱ��쳣���������ӣ���\n\n"
					"��ע��ʱ��ת���޷�����ɨӲ�̣�ֻ������cpuʹ�á�",
					"SGuard������ " VERSION "  by: @H3d9",
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
				SetWindowText(hDlg, "����������Դ�İٷֱ�");
				sprintf(buf, "��������1~99����999������99.9��\n����ǰֵ��%u��", limitMgr.limitPercent.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_TIME) { // set time slice.
				SetWindowText(hDlg, "����ÿ100ms��Ŀ���߳���ǿ�ư����ʱ�䣨��λ��ms��");
				sprintf(buf, "\n����1~99����������ǰֵ��%u��", traceMgr.lockRound.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

			} else if (dlgParam == DLGPARAM_PATCHWAIT1) { // set advanced patch wait for ntdll ioctl.
				SetWindowText(hDlg, "���뿪����ɨ�̹���ǰ�ĳ�ʼ�ȴ�ʱ�䣨��λ���룩");
				sprintf(buf, "\n����һ����������ǰ�ȴ�ʱ�䣺%u�룩", patchMgr.patchDelayBeforeNtdllioctl.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);
				SetDlgItemText(hDlg, IDC_TEXT2, "����ʾ����û�б�Ҫ�޸���һ�");
				
			} else if (dlgParam == DLGPARAM_PATCHWAIT2) { // set advanced patch wait for ntdll etc.
				SetWindowText(hDlg, "���뿪����ɨ�̹��ܺ�ȴ�SGUARD�ȶ���ʱ�䣨��λ���룩");
				sprintf(buf, "\n����һ����������ǰ�ȴ�ʱ�䣺%u�룩", patchMgr.patchDelayBeforeNtdlletc.load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);
				
			} else { // set patch delay switches.
				auto id = dlgParam - DLGPARAM_DELAY1;
				SetWindowText(hDlg, "����SGUARDÿ��ִ�е�ǰϵͳ���õ�ǿ���ӳ٣���λ��ms��");
				sprintf(buf, "\n����%u~%u����������ǰֵ��%u��", delayRange[id].low, delayRange[id].high, delay[id].load());
				SetDlgItemText(hDlg, IDC_TEXT1, buf);

				if (dlgParam == DLGPARAM_DELAY1) {
					SetDlgItemText(hDlg, IDC_TEXT2, "��ǰ���ã�NtQueryVirtualMemory");
				} else if (dlgParam == DLGPARAM_DELAY2) {
					SetDlgItemText(hDlg, IDC_TEXT2, "��ǰ���ã�GetAsyncKeyState");
				} else if (dlgParam == DLGPARAM_DELAY3) {
					SetDlgItemText(hDlg, IDC_TEXT2, "��ǰ���ã�NtWaitForSingleObject\n��ע�⡿���������ô���100����ֵ��");
				} else if (dlgParam == DLGPARAM_DELAY4) {
					SetDlgItemText(hDlg, IDC_TEXT2, "��ǰ���ã�NtDelayExecution");
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

				if (dlgParam == DLGPARAM_PCT) {
					if (!translated || res < 1 || (res > 99 && res != 999)) {
						systemMgr.panic("����1~99��999");
					} else {
						limitMgr.setPercent(res);
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_TIME) {
					if (!translated || res < 1 || res > 99) {
						systemMgr.panic("����1~99������");
					} else {
						traceMgr.lockRound = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_PATCHWAIT1) {
					if (!translated) {
						systemMgr.panic("�����ʽ����");
					} else {
						patchMgr.patchDelayBeforeNtdllioctl = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else if (dlgParam == DLGPARAM_PATCHWAIT2) {
					if (!translated) {
						systemMgr.panic("�����ʽ����");
					} else {
						patchMgr.patchDelayBeforeNtdlletc = res;
						EndDialog(hDlg, LOWORD(wParam));
						return (INT_PTR)TRUE;
					}

				} else {
					auto id = dlgParam - DLGPARAM_DELAY1;
					if (!translated || res < delayRange[id].low || res > delayRange[id].high) {
						systemMgr.panic("����%u~%u������", delayRange[id].low, delayRange[id].high);
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


			char    buf   [0x1000] = {};
			HMENU   hMenu          = CreatePopupMenu();
			HMENU   hMenuModes     = CreatePopupMenu();
			HMENU   hMenuOthers    = CreatePopupMenu();

			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_HIJACK,  "�л�����ʱ��Ƭ��ת");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_TRACE,   "�л������߳�׷��");
			AppendMenu(hMenuModes,  MFT_STRING, IDM_MODE_PATCH,   "�л������ڴ油�� " MEMPATCH_VERSION);
			if (g_Mode == 0) {
				CheckMenuItem(hMenuModes, IDM_MODE_HIJACK, MF_CHECKED);
			} else if (g_Mode == 1) {
				CheckMenuItem(hMenuModes, IDM_MODE_TRACE,  MF_CHECKED);
			} else { // if g_Mode == 2
				CheckMenuItem(hMenuModes, IDM_MODE_PATCH,  MF_CHECKED);
			}

			AppendMenu(hMenuOthers, MFT_STRING, IDM_AUTOSTARTUP,     "��������");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_KILLACELOADER,   "��Ϸ����60��󣬽���ace-loader");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_HIDESYSFILE,     "�������ļ����ص�ϵͳ�û�Ŀ¼");
			AppendMenu(hMenuOthers, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_UPDATEPAGE, "�����¡���ǰ�汾��" VERSION "��");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_ABOUT,           "�鿴˵��");
			AppendMenu(hMenuOthers, MFT_STRING, IDM_MORE_SOURCEPAGE, "�鿴Դ����");
			if (systemMgr.autoStartup) {
				CheckMenuItem(hMenuOthers, IDM_AUTOSTARTUP, MF_CHECKED);
			}
			if (systemMgr.killAceLoader) {
				CheckMenuItem(hMenuOthers, IDM_KILLACELOADER, MF_CHECKED);
			}
			if (driver.loadFromProfileDir) {
				CheckMenuItem(hMenuOthers, IDM_HIDESYSFILE, MF_CHECKED);
			}


			if (g_Mode == 0) {

				if (!limitMgr.limitEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard������ - �û��ֶ���ͣ");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard������ - �ȴ���Ϸ����");
				} else {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard������ - ��⵽SGuard");
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "��ǰģʽ��ʱ��Ƭ��ת  [����л�]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				if (limitMgr.limitPercent == 999) {
					AppendMenu(hMenu, MFT_STRING, IDM_STARTLIMIT, "������Դ��99.9%");
				} else {
					sprintf(buf, "������Դ��%u%%", limitMgr.limitPercent.load());
					AppendMenu(hMenu, MFT_STRING, IDM_STARTLIMIT, buf);
				}
				AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT,       "ֹͣ����");
				AppendMenu(hMenu, MFT_STRING, IDM_SETPERCENT,      "����������Դ�İٷֱ�");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_KERNELLIMIT,    "ʹ���ں�̬������");
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
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,     "SGuard������ - �û��ֶ���ͣ");
				} else if (g_HijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,     "SGuard������ - �ȴ���Ϸ����");
				} else {
					if (traceMgr.lockPid == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard������ - ���ڷ���");
					} else {
						sprintf(buf, "SGuard������ - ");
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
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "��ǰģʽ���߳�׷��  [����л�]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3,    "����");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1,    "������");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3RR,  "����-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1RR,  "������-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK,   "�������");
				if (traceMgr.lockMode == 1 || traceMgr.lockMode == 3) {
					AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
					sprintf(buf, "����ʱ���з֣���ǰ��%d��", traceMgr.lockRound.load());
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
					AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,      "SGuard������ - ģʽ��Ч������δ��ʼ����");
				} else {
					if (g_HijackThreadWaiting) {
						AppendMenu(hMenu, MFT_STRING, IDM_ABOUT,  "SGuard������ - �ȴ���Ϸ����");
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
								AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, "SGuard������ - ��ȴ�");
							} else {
								sprintf(buf, "SGuard������ - �������ԣ���%d�Σ�... << [����鿴��ϸ��Ϣ]", patchMgr.patchFailCount.load());
								AppendMenu(hMenu, MFT_STRING, IDM_PATCHFAILHINT, buf);
							}
						} else {
							sprintf(buf, "SGuard������ - ���ύ  [%d/%d]", finished, total);
							AppendMenu(hMenu, MFT_STRING, IDM_ABOUT, buf);
						}
					}
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuModes, "��ǰģʽ���ڴ油�� " MEMPATCH_VERSION "  [����л�]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_DOPATCH,       "�Զ�");
				AppendMenu(hMenu, MF_GRAYED, IDM_UNDOPATCH,       "�����޸�");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH1,  "inline Ntdll!NtQueryVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH2,  "inline Ntdll!NtReadVirtualMemory");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH3,  "inline User32!GetAsyncKeyState");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH4,  "inline Ntdll!NtWaitForSingleObject");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH5,  "re-write Ntdll!NtDelayExecution");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH6,  "[��ɨ��1] α��ACE-BASE.sys��MDL���ƴ���");
				AppendMenu(hMenu, drvMenuType, IDM_PATCHSWITCH7,  "[��ɨ��2] ִ��ʧ�ܵ��ļ�ϵͳ��¼ö��");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, drvMenuType, IDM_ADVMEMSEARCH, "���ø߼��ڴ�����");
				sprintf(buf, "�����ӳ٣���ǰ��%u/%u", patchMgr.patchDelayBeforeNtdllioctl.load(), patchMgr.patchDelayBeforeNtdlletc.load());
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
				strcat(buf, "��");
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
			AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMenuOthers, "�鿴˵��/����ѡ��");
			AppendMenu(hMenu, MFT_STRING, IDM_EXIT,            "�˳�");


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
		char  buf  [0x1000];
		UINT  id   = LOWORD(wParam);

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
				if (IDYES == MessageBox(0, "��ʱ��Ƭ��ת���Ǿɰ湦�ܣ����ܵ�����Ϸ���ߣ�\n����Ĭ��ģʽ�޷�ʹ��ʱ�ٻ�����ȷ��Ҫ�л���", "ע��", MB_YESNO)) {
					traceMgr.disable();
					patchMgr.disable();
					limitMgr.enable();
					g_Mode = 0;
					configMgr.writeConfig();
				}
				break;
			case IDM_MODE_TRACE:
				if (IDYES == MessageBox(0, "���߳�׷�١��Ǿɰ湦�ܣ����ܵ�����Ϸ���ߣ�\n������ʹ�øù��ܣ���ȷ��Ҫ�л���", "ע�⣺�����õĹ��ܣ�", MB_YESNO)) {
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
				MessageBox(0, "�л���ѡ����Ҫ��������������", "ע��", MB_OK);
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
				sprintf(buf, "����������ѡ����ӳ٣������������ĳѡ�����ֱ�ӹرմ��ڡ�\n\n"
					"(�߼��ڴ�����) ������ɨ�̹���ǰ�ĳ�ʼ�ȴ�ʱ��\n"
					"(�߼��ڴ�����) ������ɨ�̹��ܺ�ȴ�SGUARD�ȶ���ʱ��\n"
					"%s%s%s%s\n\n"
					"����ʾ�������֪��ĳѡ������ã�����������ã����������Ϸ����/�޷�������\n",
					patchMgr.patchSwitches.NtQueryVirtualMemory   ? "NtQueryVirtualMemory\n"   : "",
					patchMgr.patchSwitches.GetAsyncKeyState       ? "GetAsyncKeyState\n"       : "",
					patchMgr.patchSwitches.NtWaitForSingleObject  ? "NtWaitForSingleObject\n"  : "",
					patchMgr.patchSwitches.NtDelayExecution       ? "NtDelayExecution\n"       : "");

				if (IDYES == MessageBox(0, buf, "�����ӳ�", MB_YESNO)) {

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
					MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				}
			}
				break;
			case IDM_PATCHSWITCH1:
				if (patchMgr.patchSwitches.NtQueryVirtualMemory) {
					if (IDYES == MessageBox(0, "������ǡ����ر�NtQueryVirtualMemory���ء�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.patchSwitches.NtQueryVirtualMemory = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.NtQueryVirtualMemory = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH2:
				if (patchMgr.patchSwitches.NtReadVirtualMemory) {
					if (IDYES == MessageBox(0, "������ǡ����ر�NtReadVirtualMemory���ء�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.patchSwitches.NtReadVirtualMemory = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.NtReadVirtualMemory = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH3:
				if (patchMgr.patchSwitches.GetAsyncKeyState) {
					if (IDYES == MessageBox(0, "������ǡ����ر�GetAsyncKeyState���ء�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.patchSwitches.GetAsyncKeyState = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.GetAsyncKeyState = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH4:
				if (patchMgr.patchSwitches.NtWaitForSingleObject) {
					patchMgr.patchSwitches.NtWaitForSingleObject = false;
				} else {
					if (IDYES == MessageBox(0, "���棺��ѡ���ѷ�����\n\n��֪���ú���ܵ��¡�3009������96������lol���ߡ�����Ϸ�쳣��\n��Ҫ������һѡ�������֪��������ʲô��Ҫ����ô��", "���棺���������ã�", MB_YESNO)) {
						patchMgr.patchSwitches.NtWaitForSingleObject = true;
					} else {
						break;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH5:
				if (patchMgr.patchSwitches.NtDelayExecution) {
					patchMgr.patchSwitches.NtDelayExecution = false;
				} else {
					if (IDYES == MessageBox(0, "���棺��ѡ���ѷ�����\n\n��֪���ú���ܳ��֡�3009������96������ż�����١������⡣\n��Ҫ������һѡ�������֪��������ʲô��Ҫ����ô��", "���棺���������ã�", MB_YESNO)) {
						patchMgr.patchSwitches.NtDelayExecution = true;
					} else {
						break;
					}
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH6:
				if (patchMgr.patchSwitches.DeviceIoControl_1) {
					if (IDYES == MessageBox(0, "������ǡ����ر�NtDeviceIoControlFile���ء�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.patchSwitches.DeviceIoControl_1 = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.DeviceIoControl_1 = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHSWITCH7:
				if (patchMgr.patchSwitches.DeviceIoControl_2) {
					if (IDYES == MessageBox(0, "������ǡ����ر�NtFsControlFile���ء�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.patchSwitches.DeviceIoControl_2 = false;
					} else {
						break;
					}
				} else {
					patchMgr.patchSwitches.DeviceIoControl_2 = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;
			case IDM_PATCHFAILHINT:
				MessageBox(0, 
					"���֡��������ԡ�������ʾ�������޷�������SGUARDɨ�ڴ��ָ�\n"
					"��һ������SGUARD�������ʱ�䲢û��ɨ�ڴ浼�¡�\n"
					"����Ľ��������\n\n"
					"1 �򿪸߼��ڴ��������ܡ�\n"
					"2 ֱ�ӵȴ��������Զ����ԡ�\n"
					"3 ������Ϸ���������ԡ�\n"
					"4 ��һ�����ʹ����������\n"
					"5 ż�������������ġ�����ÿ��������Ϸ�������Ҿ���ʱ��������Ч��Ӧֹͣʹ��������"
					, "��Ϣ", MB_OK);
				break;
			case IDM_ADVMEMSEARCH:
				if (patchMgr.useAdvancedSearch) {
					if (IDYES == MessageBox(0, "������ǡ����رո߼��ڴ��������ܡ�\n���㲻֪�����ѡ����ش𡰷񡱡�", "ע��", MB_YESNO)) {
						patchMgr.useAdvancedSearch = false;
					} else {
						break;
					}
				} else {
					patchMgr.useAdvancedSearch = true;
				}
				configMgr.writeConfig();
				MessageBox(0, "������Ϸ����Ч", "ע��", MB_OK);
				break;

			// more options
			case IDM_AUTOSTARTUP:
			{
				sprintf(buf, "������ǡ���%s����������������", systemMgr.autoStartup ? "����" : "����");
				if (IDYES == MessageBox(0, buf, "��ʾ", MB_YESNO)) {

					// set flag and modify registry. set back if modify fail.
					systemMgr.autoStartup = !systemMgr.autoStartup;

					if (!systemMgr.modifyStartupReg()) {
						systemMgr.autoStartup = !systemMgr.autoStartup;
					}

					configMgr.writeConfig();
				}
			}
				break;
			case IDM_KILLACELOADER:
			{
				sprintf(buf, "������ǡ���%s�Զ�����ace-loader���ܡ�", systemMgr.killAceLoader ? "����" : "����");
				if (IDYES == MessageBox(0, buf, "��ʾ", MB_YESNO)) {

					systemMgr.killAceLoader = !systemMgr.killAceLoader;
					configMgr.writeConfig();
				}
			}
				break;
			case IDM_HIDESYSFILE:
			{
				sprintf(buf, "������ǡ����Խ������ļ��ļ���λ������Ϊ%s��һ�㲻Ӱ��ʹ�ã���\n\n�������ȹ���Ϸ�ٽ��иò�����",
					driver.loadFromProfileDir ? "��ǰĿ¼" : "ϵͳ�û�Ŀ¼");
				if (IDYES == MessageBox(0, buf, "��ʾ", MB_YESNO)) {

					// set flag and reload sysfile.
					driver.loadFromProfileDir = !driver.loadFromProfileDir;
					
					if (!driver.prepareSysfile()) {
						driver.loadFromProfileDir = !driver.loadFromProfileDir;
						limitMgr.useKernelMode = false;
						systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
					}

					configMgr.writeConfig();
				}
			}
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