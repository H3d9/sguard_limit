#include <Windows.h>

#include "tray.h"

extern HWND         g_hWnd;
NOTIFYICONDATA      icon        = {};


void CreateTray() {
	
	icon.cbSize = sizeof(icon);
	icon.hWnd = g_hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = WM_TRAYACTIVATE;
	icon.hIcon = (HICON)GetClassLongPtr(g_hWnd, GCLP_HICON);
	strcpy(icon.szTip, "SGuardÏÞÖÆÆ÷");

	Shell_NotifyIcon(NIM_ADD, &icon);
}

void RemoveTray() {

	Shell_NotifyIcon(NIM_DELETE, &icon);
}