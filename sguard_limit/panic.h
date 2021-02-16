#pragma once

void panic(const char*, ...);
void showErrorMessage(const char* hint, DWORD code);
void showErrorMessageInList(const char* hint, DWORD* list, DWORD n);