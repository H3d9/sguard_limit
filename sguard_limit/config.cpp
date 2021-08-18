#include <Windows.h>
#include <UserEnv.h>
#include <stdio.h>
#include "wndproc.h"  // macro: VERSION

#include "config.h"

extern volatile DWORD g_Mode;

// extern volatile bool limitEnabled;  (while selected, default to enabled.)
extern volatile DWORD limitPercent;

// extern volatile bool lockEnabled;  (while selected, default to enabled.)
extern volatile DWORD lockMode;


char confFile[4096];

bool loadConfig() {  // executes only when program is initalizing.

    bool ret = true;
    HANDLE hToken;
    char confPath[4096];
    DWORD size = 4096;
    char version[128];

    // acquire config directory and file.
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
    GetUserProfileDirectory(hToken, confPath, &size);
    sprintf(confPath + strlen(confPath), "\\AppData\\Roaming\\sguard_limit");
    strcpy(confFile, confPath);
    sprintf(confFile + strlen(confFile), "\\config.ini");
    CloseHandle(hToken);

    // if path does not exist, create a new one.
    DWORD pathAttr = GetFileAttributes(confPath);
    if ((pathAttr == INVALID_FILE_ATTRIBUTES) || !(pathAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        CreateDirectory(confPath, NULL);
        ret = false;
    }

    // check version.
    GetPrivateProfileString("Global", "Version", NULL, version, 128, confFile);
    if (strcmp(version, VERSION) != 0) {
        WritePrivateProfileString("Global", "Version", VERSION, confFile);
        ret = false;
    }

    // load configurations.
    UINT res = GetPrivateProfileInt("Global", "Mode", -1, confFile);
    if (res == (UINT)-1 || (res != 0 && res != 1)) {
        WritePrivateProfileString("Global", "Mode", "1", confFile);
        g_Mode = 1;
    } else {
        g_Mode = res;
    }

    res = GetPrivateProfileInt("Limit", "Percent", -1, confFile);
    if (res == (UINT)-1 || (res != 90 && res != 95 && res != 99 && res != 999)) {
        WritePrivateProfileString("Limit", "Percent", "90", confFile);
        limitPercent = 90;
    } else {
        limitPercent = res;
    }

    res = GetPrivateProfileInt("Lock", "Mode", -1, confFile);
    if (res == (UINT)-1 || (res != 0 && res != 1 && res != 2 && res != 3)) {
        WritePrivateProfileString("Lock", "Mode", "0", confFile);
        lockMode = 0;
    } else {
        lockMode = res;
    }

    return ret;
}

void writeConfig() {

    char buf[16];

    sprintf(buf, "%u", g_Mode);
    WritePrivateProfileString("Global", "Mode", buf, confFile);

    sprintf(buf, "%u", limitPercent);
    WritePrivateProfileString("Limit", "Percent", buf, confFile);

    sprintf(buf, "%u", lockMode);
    WritePrivateProfileString("Lock", "Mode", buf, confFile);
}
