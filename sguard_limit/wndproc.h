#pragma once
#include <Windows.h>


#define VERSION             "22.6.29 万圣节特别版"
#define MEMPATCH_VERSION    "V4.5"

#define WM_TRAYACTIVATE     WM_APP + 10U

#define IDM_ABOUT           200
#define IDM_EXIT            201

#define IDM_MODE_HIJACK     202
#define IDM_MODE_TRACE      203
#define IDM_MODE_PATCH      204


// limit wndproc button command
#define IDM_STARTLIMIT      205
#define IDM_STOPLIMIT       206
#define IDM_SETPERCENT      207
#define IDM_KERNELLIMIT     208


// trace wndproc button command
#define IDM_LOCK3           209
#define IDM_LOCK1           210
#define IDM_LOCK3RR         211
#define IDM_LOCK1RR         212
#define IDM_SETRRTIME       213
#define IDM_UNLOCK          214


// mempatch wndproc button command
#define IDM_DOPATCH         215
#define IDM_UNDOPATCH       216
#define IDM_SETDELAY        217
#define IDM_PATCHSWITCH1    218
#define IDM_PATCHSWITCH2    219
#define IDM_PATCHSWITCH3    220
#define IDM_PATCHSWITCH4    221
#define IDM_PATCHSWITCH5    222
#define IDM_PATCHSWITCH6    223
#define IDM_PATCHSWITCH7    224
#define IDM_PATCHFAILHINT   225
#define IDM_ADVMEMSEARCH    226


// other commands
#define IDM_KILLACELOADER    1000

#define IDM_MORE_UPDATEPAGE  1100
#define IDM_MORE_SOURCEPAGE  1101


// dialog param macro
#define DLGPARAM_PCT         2000
#define DLGPARAM_TIME        2001
#define DLGPARAM_PATCHWAIT1  2002
#define DLGPARAM_PATCHWAIT2  2003
#define DLGPARAM_DELAY1      2004
#define DLGPARAM_DELAY2      2005
#define DLGPARAM_DELAY3      2006
#define DLGPARAM_DELAY4      2007


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);