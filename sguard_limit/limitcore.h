#pragma once
#include <Windows.h>

DWORD GetProcessID();
bool EnumCurrentThread(DWORD);
BOOL Hijack(DWORD);