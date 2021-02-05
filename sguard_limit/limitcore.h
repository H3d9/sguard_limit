#pragma once

#include <Windows.h>

DWORD GetProcessID(const char*);
BOOL Hijack(DWORD);