#include <Windows.h>
#include <stdio.h>
#include <atomic>
#include "config.h"

#include "wndproc.h"       // macro VERSION
#include "win32utility.h"  // global options
#include "kdriver.h"       // win11 kdriver options
#include "limitcore.h"     // class LimitManager
#include "tracecore.h"     // class TraceManager
#include "mempatch.h"      // class PatchManager

// objects to write
extern std::atomic<DWORD>      g_Mode;

extern win32SystemManager&     systemMgr;
extern KernelDriver&           driver;
extern LimitManager&           limitMgr;
extern TraceManager&           traceMgr;
extern PatchManager&           patchMgr;


// config manager
ConfigManager  ConfigManager::configManager;

ConfigManager::ConfigManager() : profile{} {}

ConfigManager& ConfigManager::getInstance() {
	return configManager;
}

void ConfigManager::init(const std::string& profileDir) {
	profile = profileDir + "\\config.ini";
}

bool ConfigManager::loadConfig() {  // executes only when program is initalizing.

	auto     profile   = this->profile.c_str();
	char     version   [128];
	bool     result    = true;


	// check version & load configurations.
	GetPrivateProfileString("Global", "Version", NULL, version, 128, profile);
	if (strcmp(version, VERSION) != 0) {
		WritePrivateProfileString("Global", "Version", VERSION, profile);
		result = false;
	}

	// global config
	g_Mode                    = GetPrivateProfileInt("Global", "Mode",            2,     profile);
	systemMgr.autoStartup     = GetPrivateProfileInt("Global", "autoStartup",     FALSE, profile);
	systemMgr.killAceLoader   = GetPrivateProfileInt("Global", "KillAceLoader",   TRUE,  profile);
	systemMgr.autoCheckUpdate = GetPrivateProfileInt("Global", "autoCheckUpdate", TRUE, profile);
	

	// kdriver config
	driver.win11ForceEnable   = GetPrivateProfileInt("kdriver", "win11ForceEnable",   FALSE, profile);
	driver.win11CurrentBuild  = GetPrivateProfileInt("kdriver", "win11CurrentBuild",  0,     profile);

	// limit config
	limitMgr.limitPercent  = GetPrivateProfileInt("Limit", "Percent",       90,   profile);
	limitMgr.useKernelMode = GetPrivateProfileInt("Limit", "useKernelMode", TRUE, profile);

	// lock config
	traceMgr.lockMode  = GetPrivateProfileInt("Lock", "Mode",  0,  profile);
	traceMgr.lockRound = GetPrivateProfileInt("Lock", "Round", 95, profile);

	// patch config
	patchMgr.patchDelayBeforeNtdlletc = GetPrivateProfileInt("Patch", "DelayBeforeNtdlletc", 20, profile);

	patchMgr.patchSwitches.NtQueryVirtualMemory  = 
		GetPrivateProfileInt("Patch", "NtQueryVirtualMemory",  TRUE,  profile);
	patchMgr.patchSwitches.NtReadVirtualMemory   =
		GetPrivateProfileInt("Patch", "NtReadVirtualMemory",   TRUE,  profile);
	patchMgr.patchSwitches.GetAsyncKeyState      =
		GetPrivateProfileInt("Patch", "GetAsyncKeyState",      TRUE,  profile);
	patchMgr.patchSwitches.NtWaitForSingleObject =
		GetPrivateProfileInt("Patch", "NtWaitForSingleObject", FALSE, profile);
	patchMgr.patchSwitches.NtDelayExecution      =
		GetPrivateProfileInt("Patch", "NtDelayExecution",      FALSE, profile);
	patchMgr.patchSwitches.DeviceIoControl_1     =
		GetPrivateProfileInt("Patch", "DeviceIoControl_1",     TRUE,  profile);
	patchMgr.patchSwitches.DeviceIoControl_1x    =
		GetPrivateProfileInt("Patch", "DeviceIoControl_1x",    TRUE,  profile);
	patchMgr.patchSwitches.DeviceIoControl_2     =
		GetPrivateProfileInt("Patch", "DeviceIoControl_2",     TRUE,  profile);


	auto defDelay = [] (auto i) { return patchMgr.patchDelayRange[i].def; };

	patchMgr.patchDelay[0] = GetPrivateProfileInt("Patch", "Delay0", defDelay(0), profile);
	patchMgr.patchDelay[1] = GetPrivateProfileInt("Patch", "Delay1", defDelay(1), profile);
	patchMgr.patchDelay[2] = GetPrivateProfileInt("Patch", "Delay2", defDelay(2), profile);
	patchMgr.patchDelay[3] = GetPrivateProfileInt("Patch", "Delay3", defDelay(3), profile);
	patchMgr.patchDelay[4] = GetPrivateProfileInt("Patch", "Delay4", defDelay(4), profile);


	// if it's first time user updates to this version,
	// force to mempatch and set some options to default.

	if (!result) {

		g_Mode = 2;

		driver.win11ForceEnable   = false;
		driver.win11CurrentBuild  = 0;

		patchMgr.patchDelayBeforeNtdlletc = 20;

		patchMgr.patchSwitches.NtQueryVirtualMemory  = true;
		patchMgr.patchSwitches.NtReadVirtualMemory   = true;
		patchMgr.patchSwitches.GetAsyncKeyState      = true;
		patchMgr.patchSwitches.NtWaitForSingleObject = false;
		patchMgr.patchSwitches.NtDelayExecution      = false;
		patchMgr.patchSwitches.DeviceIoControl_1     = true;
		patchMgr.patchSwitches.DeviceIoControl_1x    = true;
		patchMgr.patchSwitches.DeviceIoControl_2     = true;

		patchMgr.patchDelay[0] = defDelay(0);
		patchMgr.patchDelay[1] = defDelay(1);
		patchMgr.patchDelay[2] = defDelay(2);
		patchMgr.patchDelay[3] = defDelay(3);
		patchMgr.patchDelay[4] = defDelay(4);

		writeConfig();
	}

	return result;
}

void ConfigManager::writeConfig() {

	auto    profile   = this->profile.c_str();
	char    buf       [128];

	// global config
	sprintf(buf, "%u", g_Mode.load());
	WritePrivateProfileString("Global", "Mode", buf, profile);

	sprintf(buf, systemMgr.autoStartup ? "1" : "0");
	WritePrivateProfileString("Global", "autoStartup", buf, profile);

	sprintf(buf, systemMgr.killAceLoader ? "1" : "0");
	WritePrivateProfileString("Global", "KillAceLoader", buf, profile);
	
	sprintf(buf, systemMgr.autoCheckUpdate ? "1" : "0");
	WritePrivateProfileString("Global", "autoCheckUpdate", buf, profile);
	
	// kdriver config
	sprintf(buf, driver.win11ForceEnable ? "1" : "0");
	WritePrivateProfileString("kdriver", "win11ForceEnable", buf, profile);

	sprintf(buf, "%u", driver.win11CurrentBuild);
	WritePrivateProfileString("kdriver", "win11CurrentBuild", buf, profile);

	// limit config
	sprintf(buf, "%u", limitMgr.limitPercent.load());
	WritePrivateProfileString("Limit", "Percent", buf, profile);

	sprintf(buf, limitMgr.useKernelMode ? "1" : "0");
	WritePrivateProfileString("Limit", "useKernelMode", buf, profile);

	// lock config
	sprintf(buf, "%u", traceMgr.lockMode.load());
	WritePrivateProfileString("Lock", "Mode", buf, profile);

	sprintf(buf, "%u", traceMgr.lockRound.load());
	WritePrivateProfileString("Lock", "Round", buf, profile);

	// patch config
	sprintf(buf, "%u", patchMgr.patchDelayBeforeNtdlletc.load());
	WritePrivateProfileString("Patch", "DelayBeforeNtdlletc", buf, profile);


	sprintf(buf, patchMgr.patchSwitches.NtQueryVirtualMemory ? "1" : "0");
	WritePrivateProfileString("Patch", "NtQueryVirtualMemory", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtReadVirtualMemory ? "1" : "0");
	WritePrivateProfileString("Patch", "NtReadVirtualMemory", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.GetAsyncKeyState ? "1" : "0");
	WritePrivateProfileString("Patch", "GetAsyncKeyState", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtWaitForSingleObject ? "1" : "0");
	WritePrivateProfileString("Patch", "NtWaitForSingleObject", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtDelayExecution ? "1" : "0");
	WritePrivateProfileString("Patch", "NtDelayExecution", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.DeviceIoControl_1 ? "1" : "0");
	WritePrivateProfileString("Patch", "DeviceIoControl_1", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.DeviceIoControl_1x ? "1" : "0");
	WritePrivateProfileString("Patch", "DeviceIoControl_1x", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.DeviceIoControl_2 ? "1" : "0");
	WritePrivateProfileString("Patch", "DeviceIoControl_2", buf, profile);


	sprintf(buf, "%u", patchMgr.patchDelay[0].load());
	WritePrivateProfileString("Patch", "Delay0", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[1].load());
	WritePrivateProfileString("Patch", "Delay1", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[2].load());
	WritePrivateProfileString("Patch", "Delay2", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[3].load());
	WritePrivateProfileString("Patch", "Delay3", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[4].load());
	WritePrivateProfileString("Patch", "Delay4", buf, profile);
}