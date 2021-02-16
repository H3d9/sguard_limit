#include <stdio.h>
#include <stdarg.h>
#include <Windows.h>

#include "panic.h"

// assert: !UNICODE

void panic(const char* format, ...) {
	char buf[1024];
	va_list arg;
	va_start(arg, format);
	vsprintf(buf, format, arg);
	va_end(arg);

	MessageBox(0, buf, 0, MB_OK);
}

void showErrorMessage(const char* hint, DWORD errorCode) {
	char* messageBuf = NULL;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&messageBuf,
		0, NULL);

	char* dialogBuf = (char*)LocalAlloc(LMEM_ZEROINIT,  strlen(messageBuf) + strlen(hint) + 100);
	sprintf(dialogBuf, "%s：(error %d)%s", hint, errorCode, messageBuf);
	MessageBox(0, dialogBuf, 0, MB_OK);

	LocalFree(messageBuf);
	LocalFree(dialogBuf);
}

void showErrorMessageInList(const char* hint, DWORD* errorList, DWORD errorCount) {
	char* dialogBuf = (char*)LocalAlloc(LMEM_ZEROINIT, strlen(hint) + 100);
	sprintf(dialogBuf, "%s，发生了%d个错误：\n", hint, errorCount);

	for (DWORD i = 0; i < errorCount; i++) {
		char* messageBuf = NULL;

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
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