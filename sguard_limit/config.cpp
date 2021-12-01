#include <Windows.h>
#include <stdio.h>
#include "config.h"

#include "wndproc.h"    // macro VERSION
#include "limitcore.h"  // class LimitManager
#include "tracecore.h"  // class TraceManager
#include "mempatch.h"   // class PatchManager

// objects to write
extern volatile DWORD    g_Mode;
extern LimitManager&     limitMgr;
extern TraceManager&     traceMgr;
extern PatchManager&     patchMgr;


// config manager
ConfigManager  ConfigManager::configManager;

ConfigManager::ConfigManager() 
	: profile{} {}

ConfigManager& ConfigManager::getInstance() {
	return configManager;
}

void  ConfigManager::init(const std::string& profileDir) {
	profile = profileDir + "\\config.ini";
}

bool ConfigManager::loadConfig() {  // executes only when program is initalizing.

	auto     profile    = this->profile.c_str();
	char     buf        [128];
	bool     result     = true;


	// check version.
	GetPrivateProfileString("Global", "Version", NULL, buf, 128, profile);
	if (strcmp(buf, VERSION) != 0) {
		WritePrivateProfileString("Global", "Version", VERSION, profile);
		result = false;
	}

	// load configurations.
	// if it's first time user updates to this version, force to mode 2.
	UINT res = GetPrivateProfileInt("Global", "Mode", -1, profile);
	if (!result || res == (UINT)-1 || (res != 0 && res != 1 && res != 2)) {
		WritePrivateProfileString("Global", "Mode", "2", profile);
		g_Mode = 2;
	} else {
		g_Mode = res;
	}

	// limit module
	res = GetPrivateProfileInt("Limit", "Percent", -1, profile);
	if (res == (UINT)-1 || res < 1 || (res > 99 && res != 999)) {
		WritePrivateProfileString("Limit", "Percent", "90", profile);
		limitMgr.limitPercent = 90;
	} else {
		limitMgr.limitPercent = res;
	}

	res = GetPrivateProfileInt("Limit", "useKernelMode", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Limit", "useKernelMode", "0", profile);
		limitMgr.useKernelMode = false;
	} else {
		limitMgr.useKernelMode = res ? true : false;
	}


	// lock module
	res = GetPrivateProfileInt("Lock", "Mode", -1, profile);
	if (res == (UINT)-1 || (res != 0 && res != 1 && res != 2 && res != 3)) {
		WritePrivateProfileString("Lock", "Mode", "0", profile);
		traceMgr.lockMode = 0;
	} else {
		traceMgr.lockMode = res;
	}

	res = GetPrivateProfileInt("Lock", "Round", -1, profile);
	if (res == (UINT)-1 || (res < 1 || res > 99)) {
		WritePrivateProfileString("Lock", "Round", "95", profile);
		traceMgr.lockRound = 95;
	} else {
		traceMgr.lockRound = res;
	}

	// patch module
	auto& delayRange = patchMgr.patchDelayRange;

	res = GetPrivateProfileInt("Patch", "Delay0", -1, profile);
	if (!result || res == (UINT)-1 || (res < delayRange[0].low || res > delayRange[0].high)) {
		sprintf(buf, "%u", delayRange[0].def);
		WritePrivateProfileString("Patch", "Delay0", buf, profile);
		patchMgr.patchDelay[0] = delayRange[0].def;
	} else {
		patchMgr.patchDelay[0] = res;
	}

	res = GetPrivateProfileInt("Patch", "Delay1", -1, profile);
	if (!result || res == (UINT)-1 || (res < delayRange[1].low || res > delayRange[1].high)) {
		sprintf(buf, "%u", delayRange[1].def);
		WritePrivateProfileString("Patch", "Delay1", buf, profile);
		patchMgr.patchDelay[1] = delayRange[1].def;
	} else {
		patchMgr.patchDelay[1] = res;
	}

	res = GetPrivateProfileInt("Patch", "Delay2", -1, profile);
	if (!result || res == (UINT)-1 || (res < delayRange[2].low || res > delayRange[2].high)) {
		sprintf(buf, "%u", delayRange[2].def);
		WritePrivateProfileString("Patch", "Delay2", buf, profile);
		patchMgr.patchDelay[2] = delayRange[2].def;
	} else {
		patchMgr.patchDelay[2] = res;
	}

	res = GetPrivateProfileInt("Patch", "Delay3", -1, profile);
	if (!result || res == (UINT)-1 || (res < delayRange[3].low || res > delayRange[3].high)) {
		sprintf(buf, "%u", delayRange[3].def);
		WritePrivateProfileString("Patch", "Delay3", buf, profile);
		patchMgr.patchDelay[3] = delayRange[3].def;
	} else {
		patchMgr.patchDelay[3] = res;
	}

	res = GetPrivateProfileInt("Patch", "NtQueryVirtualMemory", -1, profile);
	if (!result || res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtQueryVirtualMemory", "1", profile);
		patchMgr.patchSwitches.NtQueryVirtualMemory = true;
	} else {
		patchMgr.patchSwitches.NtQueryVirtualMemory = res ? true : false;
	}

	res = GetPrivateProfileInt("Patch", "GetAsyncKeyState", -1, profile);
	if (!result || res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "GetAsyncKeyState", "1", profile);
		patchMgr.patchSwitches.GetAsyncKeyState = true;
	} else {
		patchMgr.patchSwitches.GetAsyncKeyState = res ? true : false;
	}

	res = GetPrivateProfileInt("Patch", "NtWaitForSingleObject", -1, profile);
	if (!result || res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtWaitForSingleObject", "0", profile);
		patchMgr.patchSwitches.NtWaitForSingleObject = false;
	} else {
		patchMgr.patchSwitches.NtWaitForSingleObject = res ? true : false;
	}

	res = GetPrivateProfileInt("Patch", "NtDelayExecution", -1, profile);
	if (!result || res == (UINT)-1 || (res != 0 && res != 1)) {
		WritePrivateProfileString("Patch", "NtDelayExecution", "0", profile);
		patchMgr.patchSwitches.NtDelayExecution = false;
	} else {
		patchMgr.patchSwitches.NtDelayExecution = res ? true : false;
	}

	return result;
}

void ConfigManager::writeConfig() {

	auto    profile   = this->profile.c_str();
	char    buf       [16];

	sprintf(buf, "%u", g_Mode);
	WritePrivateProfileString("Global", "Mode", buf, profile);

	sprintf(buf, "%u", limitMgr.limitPercent);
	WritePrivateProfileString("Limit", "Percent", buf, profile);

	sprintf(buf, limitMgr.useKernelMode ? "1" : "0");
	WritePrivateProfileString("Limit", "useKernelMode", buf, profile);

	sprintf(buf, "%u", traceMgr.lockMode);
	WritePrivateProfileString("Lock", "Mode", buf, profile);

	sprintf(buf, "%u", traceMgr.lockRound);
	WritePrivateProfileString("Lock", "Round", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[0]);
	WritePrivateProfileString("Patch", "Delay0", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[1]);
	WritePrivateProfileString("Patch", "Delay1", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[2]);
	WritePrivateProfileString("Patch", "Delay2", buf, profile);

	sprintf(buf, "%u", patchMgr.patchDelay[3]);
	WritePrivateProfileString("Patch", "Delay3", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtQueryVirtualMemory ? "1" : "0");
	WritePrivateProfileString("Patch", "NtQueryVirtualMemory", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.GetAsyncKeyState ? "1" : "0");
	WritePrivateProfileString("Patch", "GetAsyncKeyState", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtWaitForSingleObject ? "1" : "0");
	WritePrivateProfileString("Patch", "NtWaitForSingleObject", buf, profile);

	sprintf(buf, patchMgr.patchSwitches.NtDelayExecution ? "1" : "0");
	WritePrivateProfileString("Patch", "NtDelayExecution", buf, profile);
}
