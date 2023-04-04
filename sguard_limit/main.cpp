// x64 SGUARD�������������ڸ�����Ѷ��Ϸ
// H3d9, д��2021.2.5��
#include <Windows.h>
#include <thread>
#include <atomic>
#include "resource.h"
#include "wndproc.h"
#include "win32utility.h"
#include "config.h"
#include "kdriver.h"
#include "limitcore.h"
#include "mempatch.h"


KernelDriver&           driver                  = KernelDriver::getInstance();
win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
ConfigManager&          configMgr               = ConfigManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();

std::atomic<bool>       g_HijackThreadWaiting   = true;
std::atomic<DWORD>      g_Mode                  = 2;      // 0: lim   2: patch


static void HijackThreadWorker() {
	
	systemMgr.log("hijack thread: created.");

	win32ThreadManager threadMgr;

	while (1) {

		// scan per 5 seconds when idle;
		// if process is found, trap into usr-selected mode.
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");

			// launch clean thread to kill GameLoader at appropriate time.
			if (systemMgr.killAceLoader) {
				systemMgr.raiseCleanThread();
			}
			if (systemMgr.cloudDataReady) {
				std::thread t([] { systemMgr.dieIfBlocked(systemMgr.cloudBanList); });
				t.detach();
			}

			// select mode.
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				g_HijackThreadWaiting = false;
				limitMgr.hijack();
				g_HijackThreadWaiting = true;
			}
			if (g_Mode == 2 && patchMgr.patchEnabled) {
				g_HijackThreadWaiting = false;
				patchMgr.patch();
				g_HijackThreadWaiting = true;
			}
		}

		g_HijackThreadWaiting.notify_all();   // inform main thread for blocked actions.
		Sleep(5000);    // call sys schedule | no target found, wait.
	}
}

INT WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd) {


	// initialize system module: 
	// setup dpi and raise privilege (must do first)
	// init system manager (check sington, get path and os version, init log subsystem)
	// init win32 gui (create window for callback, create tray)

	systemMgr.setupProcessDpi();

#ifndef _DEBUG

	// [prerequisite for raise privilege] acquire uac manually:
	// if not do this (but acquire in manifest), win10/11 with uac will refuse auto start-up.
	// return true if already in admin; false if not (will get admin with a restart).

	if (!systemMgr.runWithUac()) {
		return -1;
	}

#endif

	if (!systemMgr.enableDebugPrivilege()) {
		return -1;
	}

	if (!systemMgr.systemInit(hInstance)) {
		return -1;
	}

	if (!systemMgr.createWindow(WndProc, IDI_ICON1)) {
		return -1;
	}

	systemMgr.createTray(WM_TRAYACTIVATE);


	// initialize configuration module:
	// load all user options from given path.
	// after load, if modules needs to modify some config, just change and writeconfig().

	configMgr.init(systemMgr.getProfileDir());

	if (!configMgr.loadConfig()) {
		MessageBox(0,
			"������˵����\n\n"
			" �ڴ油�� " MEMPATCH_VERSION "��\n\n"
			"1. �޸���������³��֡�SYSTEM_THREAD_EXCEPTION_NOT_HANDLED��������\n\n\n"

			"����Ҫ��ʾ��\n\n"
			"1. �����������������κγ��۱����ߵ��˶���ƭ��Ŷ��\n\n"
			"2. ʹ����������ʱ��������ϸ�Ķ������ġ��������⣨�ؿ�������\n"
			"   ���������δ���������⣬���Լ�Ⱥ������775176979",
			VERSION "  by: @H3d9", MB_OK);
	}


	// show notice msgbox via cloud:
	// if update is avaliable, user will be notified.

	auto NotifyThreadCaller = [] {

		std::thread t([] {

			// wait till cloud data successfully grabbed.
			systemMgr.cloudDataReady.wait(false);
			systemMgr.dieIfBlocked(systemMgr.cloudBanList);

			// check for latest version. (user shall update manually)
			if (systemMgr.autoCheckUpdate && systemMgr.cloudVersion != VERSION) {

				auto strLatestVersion = format(
					"�������°汾��\n\n"
					"    ��ǰ�汾��" VERSION "\n"
					"    ���°汾��{}\n\n\n"
					"���°�˵����\n\n{}\n\n\n"
					"������ǡ�ǰ������ҳ�棬������񡱹رմ˴��ڡ�\n"
					"����ʾ������������½����̲˵�������ѡ��������Ƿ�����¡�",
					systemMgr.cloudVersion, systemMgr.cloudVersionDetail);

				if (IDYES == MessageBox(0, strLatestVersion.c_str(), "��⵽�°汾", MB_YESNO)) {
					ShellExecute(0, "open", systemMgr.cloudUpdateLink.c_str(), 0, 0, SW_SHOW);
				}
			}

			// show notice if exists.
			if (!systemMgr.cloudShowNotice.empty()) {
				MessageBox(0, systemMgr.cloudShowNotice.c_str(), "����", MB_OK);
				configMgr.writeConfig();
			}
		});
		t.detach();
	};

	NotifyThreadCaller();


	// initialize system module global functions (depending on config):
	// modify registry key to make sure auto start or not.
	// return value here is not critical.

	systemMgr.modifyStartupReg();


	// initialize kdriver module:
	// (if os supported) set registry, copy sys file, check sys version.
	
	auto DriverOptionsSelected = [] ()->bool {
		return g_Mode == 2 || (g_Mode == 0 && limitMgr.useKernelMode);
	};

	if (systemMgr.getSystemVersion() == OSVersion::OTHERS) {

		// driver not supported on this system, don't call driver.init().
		// if selected related options, show panic.
		if (DriverOptionsSelected()) {
			systemMgr.panic("�ں�����ģ������Ĳ���ϵͳ�ϲ���֧�֡�\n"
			                "��ע������ģ��֧��win7/8/8.1/10/11��");
		}

	} else {

		auto result = 
		driver.init(systemMgr.getProfileDir());

		// if driver init failed, and selected related options,
		// turn off related config flags and show error hint.
		if (!result && DriverOptionsSelected()) {
			limitMgr.useKernelMode = false;
			systemMgr.panic(result.error());
		}

		// if init success but is win11 latest, show alert.
		constexpr auto supportedLatestBuildNum = 22621;

		if (result &&
			systemMgr.getSystemVersion() == OSVersion::WIN_10_11 && 
			systemMgr.getSystemBuildNum() > supportedLatestBuildNum) {

			// if force enable bit not set, but user selected related options (first run default),
			// or force enable bit set, but build num not match (system updated),
			if ((!driver.win11ForceEnable && DriverOptionsSelected()) ||
				(driver.win11ForceEnable && systemMgr.getSystemBuildNum() != driver.win11CurrentBuild)) {
				
				// alert user to confirm potential bsod threat.
				auto strBsodAlert = format(
					"������������ϸ�Ķ���Ǳ�ڵ��������գ�������\n\n\n"
					"��ǰϵͳ�汾�����ں�����ģ����ȷ��֧�ֵ����ϵͳ�汾��\n\n"
					"��ȷ��֧�ֵ�Win11�汾��10.0.{}\n"
					"��ǰWin11ϵͳ�汾��10.0.{}\n\n\n"
					"����������Ϸ���Ҽ��˵���ʾ���ύ����ʾ���ݣ��ҿ��Ա�֤�´�ϵͳ����ǰ��û���⡣\n\n"
					"��ÿ����Ϸ����ʱ����������ʾ�ں�����ģ�鲻�ټ��ݡ�����Է�����Ⱥ�\n\n\n"
					"��������˽���������������Գе��������գ��������ǡ��������������񡱡�",
					supportedLatestBuildNum, systemMgr.getSystemBuildNum());

				if (IDYES == MessageBox(0, strBsodAlert.c_str(), "ϵͳ�汾����", MB_YESNO)) {
					driver.driverReady        = true;
					driver.win11ForceEnable   = true;
					driver.win11CurrentBuild  = systemMgr.getSystemBuildNum();

				} else {
					driver.driverReady        = false;
					driver.win11ForceEnable   = false;
					driver.win11CurrentBuild  = 0;
				}

				configMgr.writeConfig();
			}
		}
	}


	// initialize patch module:
	// get all native syscall numbers it should use.

	if (!patchMgr.init()) {

		// in some rare case (such as rtlgetversion fails by some kernel internal bug),
		// function may not work correctly. user shall restart program.
		systemMgr.panic("���ڴ油�� " MEMPATCH_VERSION "��ģ���ʼ��ʧ�ܡ�\n"
			            "����ʾ���뽫�������ƶ�������Ŀ¼���������С�");

		driver.driverReady = false;
	}


	// create working thread:
	// using std::thread (_beginthreadex) is more safe than winapi CreateThread;
	// because we use heap and crt functions in working thread.

	auto HijackThreadCaller = [] {
		std::thread hijackThread(HijackThreadWorker);
		hijackThread.detach();
	};

	HijackThreadCaller();


	// enter primary msg loop:
	// main thread will wait for window msgs from user, while working thread do actual works.

	auto ret =
	systemMgr.messageLoop();


	// program exit:
	// after winmain() returns, all static sington objects are destructed.

	systemMgr.removeTray();

	return (INT) ret;
}