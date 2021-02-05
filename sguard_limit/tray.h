#pragma once

#include <Windows.h>

#define WM_TRAYACTIVATE		WM_APP + 10U

void CreateTray();
void RemoveTray();