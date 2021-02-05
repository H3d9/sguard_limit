#pragma once

#include <Windows.h>

#define IDM_TITLE			200
#define IDM_EXIT			201
#define IDM_PERCENT90		202
#define IDM_PERCENT95		203
#define IDM_PERCENT99		204
#define IDM_PERCENT999		205
#define IDM_STOPLIMIT		206

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);