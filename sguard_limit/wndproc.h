#pragma once

#include <Windows.h>

#define IDM_TITLE			200
#define IDM_EXIT			201
#define IDM_SWITCHMODE		202

#define IDM_PERCENT90		203
#define IDM_PERCENT95		204
#define IDM_PERCENT99		205
#define IDM_PERCENT999		206
#define IDM_STOPLIMIT		207

#define IDM_LOCK1			208
#define IDM_LOCK3			209
#define IDM_UNLOCK			210

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);