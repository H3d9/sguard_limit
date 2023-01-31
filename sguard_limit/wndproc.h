#pragma once
#include <Windows.h>


#define VERSION             "23.1.31 Ô­Éñ¶á¹Ú¼ÍÄî°æ"
#define MEMPATCH_VERSION    "V4.9"

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
#define IDM_PATCHSWITCH6_1  223
#define IDM_PATCHSWITCH6_2  224
#define IDM_PATCHSWITCH6_3  225
#define IDM_PATCHSWITCH7    226
#define IDM_PATCHSWITCH8    227
#define IDM_ADVMEMSEARCH    228


// other commands
#define IDM_AUTOSTARTUP       1001
#define IDM_AUTOCHECKUPDATE   1002
#define IDM_KILLACELOADER     1003
#define IDM_OPENPROFILEDIR    1004

#define IDM_MORE_UPDATEPAGE   1100
#define IDM_MORE_SOURCEPAGE   1101
#define IDM_DONATE            1200


// dialog param macro
#define DLGPARAM_RRPCT          2001
#define DLGPARAM_LOCKTIME       2002
#define DLGPARAM_PATCHWAIT      2003
#define DLGPARAM_PATCHDELAY1    2004
#define DLGPARAM_PATCHDELAY2    2005
#define DLGPARAM_PATCHDELAY3    2006
#define DLGPARAM_PATCHDELAY4    2007
#define DLGPARAM_PATCHDELAY5    2008


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);