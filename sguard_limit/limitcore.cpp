#include "limitcore.h"
#include <Windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <Windowsx.h> 
#include <stdio.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <process.h>
#include <time.h>
#include <locale.h>
#include <Psapi.h>

#pragma comment(lib, "Advapi32.lib") // invoke OpenProcessToken

DWORD GetProcessID(const char* szProcessName) { // ret == 0 if no proc.
	PROCESSENTRY32 ps = { 0 };
	ps.dwSize = sizeof(PROCESSENTRY32);

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	DWORD targetPID = 0;

	if (!Process32First(hSnapshot, &ps)) {
		return 0;
	}

	do {
		if (lstrcmpi(ps.szExeFile, szProcessName) == 0) {
			targetPID = ps.th32ProcessID;
			// assert: only 1 pinstance.
			break;
		}
	} while (Process32Next(hSnapshot, &ps));
	CloseHandle(hSnapshot);

	return targetPID;
}

DWORD  threadIDList[512];
DWORD  numThreads;

static BOOL EnumCurrentThread(DWORD pid) { // => threadIDList & numThreads

	numThreads = 0;
	THREADENTRY32 te;
	te.dwSize = sizeof(THREADENTRY32);

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	for (BOOL next = Thread32First(hSnapshot, &te); next; next = Thread32Next(hSnapshot, &te)) {
		if (te.th32OwnerProcessID == pid) {
			threadIDList[numThreads++] = te.th32ThreadID;
		}
	}

	CloseHandle(hSnapshot);

	return TRUE;
}

HANDLE threadHandleList[512];
bool suspended = false;
DWORD suspendRetry = 0;
DWORD resumeRetry = 0;
volatile DWORD limitPercent = 90;
volatile DWORD limitWorking = true;

BOOL Hijack(DWORD pid) {

	HANDLE
		hProcess = OpenProcess(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF, FALSE, pid);
	if (!hProcess) {
		hProcess = OpenProcess(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFF, FALSE, pid);
		if (!hProcess) {
			hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
			if (!hProcess) {
				hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
			} else {
				return FALSE;
			}
		}
	}

	// assert: process is alive.
	while (limitWorking) {
		EnumCurrentThread(pid); // note: 每10+秒重新枚举线程
		if (numThreads == 0) {
			return TRUE; // process is no more alive, exit.
		}

		DWORD numOpenedThreads = 0;
		DWORD openThreadRetry = 0;
		ZeroMemory(threadHandleList, sizeof(threadHandleList));

		while (1) {
			DWORD ERROR_THREAD_OPEN = 0;
			for (DWORD i = 0; i < numThreads; i++) {
				if (!threadHandleList[i]) {
					threadHandleList[i] = OpenThread(STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3FF, FALSE, threadIDList[i]);
					if (!threadHandleList[i]) {
						threadHandleList[i] = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadIDList[i]);
						if (!threadHandleList[i]) {
							ERROR_THREAD_OPEN = 1;
						}
					}
					if (threadHandleList[i]) {
						++numOpenedThreads;
					}
				}
			}

			if ((numOpenedThreads == numThreads)
				||
				(numOpenedThreads != 0 && openThreadRetry > 10)) {
				break; // continue on.
			}
			if (ERROR_THREAD_OPEN) {
				++openThreadRetry;
			}
			if (openThreadRetry > 10) {
				// no thread is opened, exit.
				return FALSE;
			}

			// open-thread retry internal.
			Sleep(100);
		}

		// assert: !threadHandleList.empty && threadHandleList[elem].valid
		// each loop we manipulate 10+s in target process.
		for (DWORD msElapsed = 0; limitWorking && msElapsed < 10000;) {

			DWORD TimeRed = limitPercent;
			DWORD TimeGreen = 100 - TimeRed;
			if (limitPercent >= 100) {
				TimeGreen = 1;
			}

			if (!suspended) {
				DWORD ERROR_SUSPEND = 0;
				for (DWORD i = 0; i < numThreads; i++) {
					if (threadHandleList[i]) {
						if (SuspendThread(threadHandleList[i]) != (DWORD)-1) {
							suspended = true; // true if at least one of threads is suspended.
						} else {
							ERROR_SUSPEND = 1;
						}
					}
				}
				if (ERROR_SUSPEND) {
					++suspendRetry;
				} else {
					suspendRetry = 0;
				}
			}

			Sleep(TimeRed);
			msElapsed += TimeRed;

			if (suspended) {
				DWORD ERROR_RESUME = 0;
				for (DWORD i = 0; i < numThreads; i++) {
					if (threadHandleList[i]) {
						if (ResumeThread(threadHandleList[i]) != (DWORD)-1) {
							suspended = false;
						} else {
							ERROR_RESUME = 1;
						}
					}
				}
				if (ERROR_RESUME) {
					++resumeRetry;
				} else {
					resumeRetry = 0;
				}
			}

			Sleep(TimeGreen);
			msElapsed += TimeGreen;
		}

		for (DWORD i = 0; i < numThreads; ++i) { // release handles; re-capture them in next loop.
			if (threadHandleList[i]) {
				CloseHandle(threadHandleList[i]);
			}
		}

		if (suspendRetry > 100 || resumeRetry > 50) {
			// always fail(more than 10 loop WITHOUT success), jump out.
			return FALSE;
		}
	}

	// user stopped limitng, exit to wait.
	return TRUE;
}
