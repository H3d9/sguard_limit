#pragma once
#include <Windows.h>


#define VERSION             "21.11.27 万圣节特别版"

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


#define IDM_MORE_UPDATEPAGE 222
#define IDM_MORE_SOURCEPAGE 223


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);