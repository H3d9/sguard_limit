#include "tray.h"

extern HWND g_hWnd;
extern HINSTANCE g_hInstance;

void CreateTray() {
	NOTIFYICONDATA icon = {};
	icon.cbSize = sizeof(icon);
	icon.hWnd = g_hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = WM_TRAYACTIVATE;
	icon.hIcon = (HICON)GetClassLongPtr(g_hWnd, GCLP_HICON);
	strcpy(icon.szTip, "SGuard限制工具");

	Shell_NotifyIcon(NIM_ADD, &icon);
}

void RemoveTray() {
	NOTIFYICONDATA icon = {};
	icon.cbSize = sizeof(icon);
	icon.hWnd = g_hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = WM_TRAYACTIVATE;
	icon.hIcon = (HICON)GetClassLongPtr(g_hWnd, GCLP_HICON);
	strcpy(icon.szTip, "SGuard限制工具");

	Shell_NotifyIcon(NIM_DELETE, &icon);
}