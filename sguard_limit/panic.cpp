#include <stdio.h>
#include <stdarg.h>
#include <Windows.h>

#include "panic.h"

void panic(const char* format, ...) {
	// panic() shows error message only.
	char buf[1024];
	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);
	MessageBox(0, buf, 0, MB_OK);
}