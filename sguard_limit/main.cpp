// x64 SGUARD�������������ڸ�����Ѷ��Ϸ
// H3d9, д��2021.2.5��
#include <Windows.h>
#include <thread>
#include "resource.h"
#include "wndproc.h"
#include "win32utility.h"
#include "config.h"
#include "kdriver.h"
#include "limitcore.h"
#include "tracecore.h"
#include "mempatch.h"


KernelDriver&           driver                  = KernelDriver::getInstance();
win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
ConfigManager&          configMgr               = ConfigManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
TraceManager&           traceMgr                = TraceManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();

volatile bool           g_HijackThreadWaiting   = true;
volatile DWORD          g_Mode                  = 2;      // 0: lim  1: lock  2: patch

volatile bool           g_KillAceLoader         = true;


static void HijackThreadWorker() {
	
	systemMgr.log("hijack thread: created.");

	win32ThreadManager threadMgr;

	while (1) {

		// scan per 5 seconds when idle; 
		// if process is found, trap into usr-selected mode.
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");

			// launch clean thread to kill GameLoader at appropriate time.
			if (g_KillAceLoader) {
				systemMgr.raiseCleanThread();
			}

			// select mode.
			if (g_Mode == 0 && limitMgr.limitEnabled) {
				g_HijackThreadWaiting = false;
				limitMgr.hijack();
				g_HijackThreadWaiting = true;
			}
			if (g_Mode == 1 && traceMgr.lockEnabled) {
				g_HijackThreadWaiting = false;
				traceMgr.chase();
				g_HijackThreadWaiting = true;
			}
			if (g_Mode == 2 && patchMgr.patchEnabled) {
				g_HijackThreadWaiting = false;
				patchMgr.patch();
				g_HijackThreadWaiting = true;
			}
		}

		Sleep(3000); // call sys schedule | no target found, wait.
		// note: volatiles are written by single thread and read by multi thread. no need to sync.
	}
}


INT WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd) {

	bool status;


	// initialize system module: 
	// setup dpi and raise privilege (must do first)
	// init system manager (check sington, get path and os version, init log subsystem)
	// init win32 gui (create window for callback, create tray)

	systemMgr.setupProcessDpi();

	status =
	systemMgr.enableDebugPrivilege();

	if (!status) {
		return -1;
	}

	status =
	systemMgr.systemInit(hInstance);

	if (!status) {
		return -1;
	}

	status =
	systemMgr.createWindow(WndProc, IDI_ICON1);
	
	if (!status) {
		return -1;
	}

	systemMgr.createTray(WM_TRAYACTIVATE);


	// initialize configuration module:
	// load all user options from given path.
	// after load, if modules needs to modify some config, just change and writeconfig().

	configMgr.init(systemMgr.getProfileDir());

	status =
	configMgr.loadConfig();

	if (!status) {
		MessageBox(0,
			"������˵����\n\n"
			" �ڴ油�� " MEMPATCH_VERSION "����ǿ��SGɨ�ڴ档\n\n"
			"1. �������ܾ�SG��Ӧ�ò����̶��ڴ档\n"
			"2. ���������Ի��������������������µĵ��ߡ�\n\n\n"
			
			"����Ҫ��ʾ��\n\n"
			"1. �����������������κγ��۱����ߵ��˶���ƭ��Ŷ��\n\n"
			"2. Ĭ��ģʽΪ�ڴ油�������þͲ�Ҫ��ģʽ�����������Ϸ���ߡ�\n\n"
			"3. ���߼��ڴ�������Ĭ����Ϸ����ʱ����������ɨ�̣�Ȼ��ȴ��ȶ������������ܡ�\n"
			"   �������Ϸ�������������Ե��ߡ��ȴ�SGUARD�ȶ���ʱ�䡱��Ĭ��Ϊ20�룩��\n\n"
			"4. �����һ��ʹ���°棬�������ϸ�Ķ�˵�������������Ҽ��˵����ҵ�����",
			VERSION "  by: @H3d9", MB_OK);
	}


	// initialize kdriver module:
	// make preparations for load kernel driver from given path.
	// however, if (driver init fails && user selected related options), modify config and panic.

	if (systemMgr.getSystemVersion() == OSVersion::OTHERS) {
		systemMgr.panic(0, "�ں�����ģ������Ĳ���ϵͳ�ϲ���֧�֡�\n"
			               "��ע���ں�������֧��win7/10/11ϵͳ��");
	} else {

		status =
		driver.init(systemMgr.getProfileDir());

		if (!status && (g_Mode == 2 || (g_Mode == 0 && limitMgr.useKernelMode))) {

			// turn off related config flags.
			limitMgr.useKernelMode = false;
			configMgr.writeConfig();

			// show panic: alert usr to switch options manually.
			systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
			systemMgr.panic(0, "����������ʼ��ʧ�ܣ����¹���ģ���޷�ʹ�ã�\n\n"
							   "�ڴ油�� " MEMPATCH_VERSION "\n"
							   "�ں�̬������\n");
		}
	}


	// create working thread:
	// using std::thread (_beginthreadex) is more safe than winapi CreateThread;
	// because we use heap and crt functions in working thread.

	auto HijackThreadCaller = [] () {
		std::thread hijackThread(HijackThreadWorker);
		hijackThread.detach();
	};

	HijackThreadCaller();


	// enter primary msg loop:
	// main thread will wait for window msgs from usr, while working thread do actual works.

	auto result =
	systemMgr.messageLoop();


	// program exit:
	// after winmain() returns, all static sington objects are destructed.

	systemMgr.removeTray();

	return (INT) result;
}