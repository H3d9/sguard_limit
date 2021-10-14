#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>

#include "panic.h"


// assert: !UNICODE

void panic(const char* format, ...) {
	char buf[2048];
	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	DWORD error = GetLastError();

	char* description = NULL;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&description,
		0, NULL);

	sprintf(buf + strlen(buf), "\n\n发生的错误：(0x%x)%s", error, description);
	LocalFree(description);

	MessageBox(0, buf, 0, MB_OK);
}

void showErrorMessageInList(const char* hint, DWORD* errorList, DWORD errorCount) {
	char* dialogBuf = (char*)LocalAlloc(LMEM_ZEROINIT, strlen(hint) + 100);
	sprintf(dialogBuf, "%s，发生了%d个错误：\n", hint, errorCount);

	for (DWORD i = 0; i < errorCount; i++) {
		char* messageBuf = NULL;

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
			errorList[i],
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&messageBuf,
			0, NULL);

		// concreate new error message to allocated dialog buffer.
		char* newDialogBuf = (char*)LocalAlloc(LMEM_ZEROINIT, strlen(dialogBuf) + strlen(messageBuf) + 100);
		strcpy(newDialogBuf, dialogBuf);
		sprintf(newDialogBuf + strlen(newDialogBuf), "(error %d)", errorList[i]);
		strcat(newDialogBuf, messageBuf);
		LocalFree(messageBuf);

		// swap dialog buffer.
		LocalFree(dialogBuf);
		dialogBuf = newDialogBuf;
	}

	MessageBox(0, dialogBuf, 0, MB_OK);
	LocalFree(dialogBuf);
}