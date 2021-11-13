#pragma once
#include <Windows.h>


#define VERSION             "21.11.13 万圣节特别版"

#define WM_TRAYACTIVATE     WM_APP + 10U

#define IDM_ABOUT           200
#define IDM_EXIT            201

#define IDM_MODE_HIJACK     202
#define IDM_MODE_TRACE      203
#define IDM_MODE_PATCH      204


// limit wndproc button command
#define IDM_PERCENT90       205
#define IDM_PERCENT95       206
#define IDM_PERCENT99       207
#define IDM_PERCENT999      208
#define IDM_STOPLIMIT       209


// trace wndproc button command
#define IDM_LOCK3           210
#define IDM_LOCK1           211
#define IDM_LOCK3RR         212
#define IDM_LOCK1RR         213
#define IDM_SETRRTIME       214
#define IDM_UNLOCK          215


// mempatch wndproc button command
#define IDM_DOPATCH         216
#define IDM_UNDOPATCH       217
#define IDM_SETDELAY        218
#define IDM_PATCHSWITCH1    219
#define IDM_PATCHSWITCH2    220
#define IDM_PATCHSWITCH3    221
#define IDM_PATCHSWITCH4    222


#define IDM_MORE_UPDATEPAGE 223
#define IDM_MORE_SOURCEPAGE 224


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);