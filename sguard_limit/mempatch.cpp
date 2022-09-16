// Memory Patch���û�̬ģ�飩
// 2021.10.4 ��
// ����Ի������ˣ����ۡ����� 2.2 ���̺��ң����ġ�
// 2021.11.27 24:00
// ����еĽ������������������������¶ˮ��ǽ�����ᡣ
#include <Windows.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <unordered_map>
#include <algorithm>   // std::sort
#include <memory>      // std::unique_ptr
#include "mempatch.h"

// dependencies
#include "kdriver.h"
#include "win32utility.h"

extern KernelDriver&          driver;
extern win32SystemManager&    systemMgr;


// mempatch module
PatchManager  PatchManager::patchManager;

PatchManager::PatchManager()
	: patchEnabled(true), patchPid(0), patchFailCount(),
	  patchSwitches{}, patchStatus{}, patchDelay{},
	  patchDelayRange {
	   { 1,   1500, 2000 },   /* NtQueryVirtualMemory */
	   { 1,   500,  1000 },   /* GetAsyncKeyState */
	   { 1,   50,   100  },   /* NtWaitForSingleObject */
	   { 500, 1250, 2000 },   /* NtDelayExecution */
	   { 500, 1500, 5000 }    /* DeviceIoControl_1x */
	  }, 
	  useAdvancedSearch(true), patchDelayBeforeNtdllioctl(0), patchDelayBeforeNtdlletc(20), 
	  syscallTable{} {}

PatchManager& PatchManager::getInstance() {
	return patchManager;
}

bool PatchManager::init() {
	
	bool ret = true;

	// acquire syscall numbers we need.
	syscallTable["NtQueryVirtualMemory"]   = _getSyscallNumber("NtQueryVirtualMemory",  "Ntdll.dll");
	syscallTable["NtReadVirtualMemory"]    = _getSyscallNumber("NtReadVirtualMemory",   "Ntdll.dll");
	syscallTable["NtWaitForSingleObject"]  = _getSyscallNumber("NtWaitForSingleObject", "Ntdll.dll");
	syscallTable["NtDelayExecution"]       = _getSyscallNumber("NtDelayExecution",      "Ntdll.dll");
	syscallTable["NtDeviceIoControlFile"]  = _getSyscallNumber("NtDeviceIoControlFile", "Ntdll.dll");
	syscallTable["NtFsControlFile"]        = _getSyscallNumber("NtFsControlFile",       "Ntdll.dll");

	if (systemMgr.getSystemVersion() == OSVersion::WIN_10_11) {
		syscallTable["NtUserGetAsyncKeyState"] = _getSyscallNumber("NtUserGetAsyncKeyState", "win32u.dll");
		// win10 <= 10586 has no win32u (but long jmp to a syscall stub set in user32, not nearby);
		// in that case use hard code (old design has deprecated and won't change after all)...
		if (0 == syscallTable["NtUserGetAsyncKeyState"] && systemMgr.getSystemBuildNum() <= 10586) {
			syscallTable["NtUserGetAsyncKeyState"] = 0x1047;
		}
	} else {
		syscallTable["NtUserGetAsyncKeyState"] = _getSyscallNumber("GetAsyncKeyState", "User32.dll");
	}
	

	// check if there's any fail while getting syscall numbers.
	for (auto& it : syscallTable) {
		systemMgr.log("patch::init(): system call: %s -> 0x%x", it.first.c_str(), it.second);
		if (it.second == 0) {
			systemMgr.panic(0, "patch::init(): �Ӻ��� %s �л�ȡ����ϵͳ���ñ��ʧ�ܡ�", it.first.c_str());
			ret = false;
		}
	}

	return ret;
}

void PatchManager::patch() {

	win32ThreadManager     threadMgr;
	auto                   pid          = threadMgr.getTargetPid();
	
	
	// check if kernel driver is initialized.
	if (!driver.driverReady) {
		systemMgr.log("patch(): kdriver is not initialized correctly, quit.");
		return;
	}


	systemMgr.log("patch(): entering.");

	if (pid != 0         /* target exist */ &&
		pid != patchPid  /* target is not current */) {

		// reset status.
		patchPid                             = 0;
		patchStatus.NtQueryVirtualMemory     = false;
		patchStatus.NtReadVirtualMemory      = false;
		patchStatus.GetAsyncKeyState         = false;
		patchStatus.NtWaitForSingleObject    = false;
		patchStatus.NtDelayExecution         = false;
		patchStatus.DeviceIoControl_1        = false;
		patchStatus.DeviceIoControl_1x       = false;
		patchStatus.DeviceIoControl_2        = false;


		// start driver.
		if (!driver.load()) {
			systemMgr.panic(driver.errorCode, "patch().driver.load(): %s", driver.errorMessage);
			return;
		}

		// wait if sample rip: wait for stable as target just start up.
		for (auto time = 0; !useAdvancedSearch && time < 10; time++) {
			Sleep(1000);
			if (!patchEnabled || pid != threadMgr.getTargetPid()) {
				systemMgr.log("patch(): primary wait: pid not match or patch disabled, quit.");
				return;
			}
		}

		// wait if adv search: before patch ioctl.
		/*if (useAdvancedSearch) {
			systemMgr.log("patch(): waiting %us before manip ntdll ioctl.", patchDelayBeforeNtdllioctl.load());

			for (DWORD time = 0; time < patchDelayBeforeNtdllioctl; time++) {
				Sleep(1000);
				if (!patchEnabled || pid != threadMgr.getTargetPid()) {
					systemMgr.log("patch(): primary wait: pid not match or patch disabled, quit.");
					return;
				}
			}
		}*/

		// patch ioctl.
		if (patchSwitches.DeviceIoControl_1 || patchSwitches.DeviceIoControl_2) {

			patchSwitches_t switches;
			switches.DeviceIoControl_1      = patchSwitches.DeviceIoControl_1.load();
			switches.DeviceIoControl_1x     = patchSwitches.DeviceIoControl_1x.load();
			switches.DeviceIoControl_2      = patchSwitches.DeviceIoControl_2.load();

			if (!_patch_ntdll(pid, switches)) {
				driver.unload(); // if _patch_ntdll() fails, stop driver and quit (to retry).
				return;
			}
		}

		// wait if adv search: before patch ntdll etc.
		if (useAdvancedSearch) {
			systemMgr.log("patch(): waiting %us before manip ntdll etc.", patchDelayBeforeNtdlletc.load());

			for (DWORD time = 0; time < patchDelayBeforeNtdlletc; time++) {
				Sleep(1000);
				if (!patchEnabled || pid != threadMgr.getTargetPid()) {
					systemMgr.log("patch(): primary wait: pid not match or patch disabled, quit.");
					return;
				}
			}
		}

		// patch ntdll etc. (v2 features)
		if (patchSwitches.NtQueryVirtualMemory  || patchSwitches.NtReadVirtualMemory ||
			patchSwitches.NtWaitForSingleObject || patchSwitches.NtDelayExecution) {

			patchSwitches_t switches;
			switches.NtQueryVirtualMemory   = patchSwitches.NtQueryVirtualMemory.load();
			switches.NtReadVirtualMemory    = patchSwitches.NtReadVirtualMemory.load();
			switches.NtWaitForSingleObject  = patchSwitches.NtWaitForSingleObject.load();
			switches.NtDelayExecution       = patchSwitches.NtDelayExecution.load();

			if (!_patch_ntdll(pid, switches)) {
				driver.unload(); // if _patch_ntdll() fails, stop driver and quit (to retry).
				return;
			}
		}


		// patch user32 (v3 features)
		if (patchSwitches.GetAsyncKeyState) {

			patchSwitches_t switches;
			switches.GetAsyncKeyState = patchSwitches.GetAsyncKeyState.load();

			if (!_patch_user32(pid, switches)) {
				systemMgr.log("patch(): warning: _patch_user32() failed!");
			}
		}


		// stop driver.
		driver.unload();

		patchPid = pid;
		systemMgr.log("patch(): all operation complete.");
	}


	systemMgr.log("patch(): fall in wait loop.");

	while (patchEnabled) {

		pid = threadMgr.getTargetPid();

		if (pid == 0 /* target no more exists */ || pid != patchPid /* target is not current */) {
			patchPid = 0;
			break;
		}

		Sleep(5000);
	}

	systemMgr.log("patch(): leave.");
}

void PatchManager::enable(bool forceRecover) {
	patchEnabled = true;
}

void PatchManager::disable(bool forceRecover) {
	patchEnabled = false;
}

DWORD PatchManager::_getSyscallNumber(const char* funcName, const char* libName) {

	DWORD callNumber = 0;

	auto hModule = LoadLibrary(libName);
	if (hModule) {

		auto procAddr = (char*)GetProcAddress(hModule, funcName);
		if (procAddr) {

			// if is Nt/Zw func (__kernelentry), pattern is at header+0.
			// otherwise, search nearby (win7/8/8.1, in user32; ignore win10 <= 10586).
			for (auto rip = procAddr; rip < procAddr + 0x200; rip++) {
				if (0 == memcmp(rip, "\x4c\x8b\xd1\xb8", 4) && 0 == memcmp(rip + 6, "\x00\x00", 2)) {
					callNumber = *(DWORD*)(rip + 4);
					break;
				}
			}
		}

		FreeLibrary(hModule);
	}

	return callNumber;
}

bool PatchManager::_patch_ntdll(DWORD pid, patchSwitches_t& switches) {

	win32ThreadManager     threadMgr;
	ULONG64                vmStartAddress    = NULL;
	auto                   vmbuf_ptr         = std::make_unique<char[]>(0x4000);
	auto                   vmalloc_ptr       = std::make_unique<char[]>(0x4000);

	auto                   osVersion         = systemMgr.getSystemVersion();
	auto                   osBuildNum        = systemMgr.getSystemBuildNum();
	auto                   vmbuf             = vmbuf_ptr.get();
	auto                   vmalloc           = vmalloc_ptr.get();
	
	patchStatus_t          patchedNow;
	bool                   status;

	
	// assert: driver loaded.
	systemMgr.log("patch_ntdll(): entering.");

	// if use normal search, before stable, wait a second.
	for (auto time = 0; !useAdvancedSearch && time < 5; time++) {
		Sleep(1000);
		if (!patchEnabled || pid != threadMgr.getTargetPid()) {
			systemMgr.log("patch_ntdll(): primary wait: pid not match or patch disabled, quit.");
			return false;
		}
	}


	// find ntdll syscall 0 offset.
	// offset0: syscall entry rva from vmbuf:0.
	LONG offset0 = -1;

	// try multi-times to find target offset.
	for (auto try_times = 1; try_times <= 3; try_times++) {

		std::vector<ULONG64> rips;
		rips.clear();

		if (useAdvancedSearch) {
			systemMgr.log("patch_ntdll(): [Caution] using Advanced memory search.");

			// search memory executable modules in given image from kernel structs.
			std::vector<ULONG64> executeRange;

			status =
			driver.searchVad(pid, executeRange, L"Ntdll.dll");

			if (!status) {
				systemMgr.panic(driver.errorCode, "patch_ntdll(): �ڴ�ɨ��ʧ��: %s", driver.errorMessage);
			}

			// check if result exists.
			if (executeRange.empty()) {
				systemMgr.panic("patch_ntdll(): �޷���Ŀ��������ҵ�ģ�顰Ntdll��");
			}

			// split executable range to pieces to read.
			for (size_t i = 0; i < executeRange.size(); i += 2) {

				auto moduleVABegin = executeRange[i];
				auto moduleVAEnd = executeRange[i + 1];

				for (auto moduleVA = moduleVABegin + 0x1000; moduleVA < moduleVAEnd; moduleVA += 0x1000) {
					rips.push_back(moduleVA);
				}
			}

		} else {
			systemMgr.log("patch_ntdll(): using normal rip search.");

			// get potential rip in top 3 threads like before.
			rips = _findRip();
		}

		if (rips.empty()) {
			systemMgr.log("patch_ntdll(): rips/blocks empty, quit.");
			return false;
		}


		// search memory traits in all rip found.
		for (auto rip = rips.begin(); rip != rips.end(); ++rip) {

			// round up page.
			vmStartAddress = (*rip & ~0xfff) - 0x1000;


			// read memory.
			status =
			driver.readVM(pid, vmbuf, (PVOID)vmStartAddress);

			if (!status) {
				systemMgr.log(driver.errorCode, "patch_ntdll() warning: load memory failed at 0x%llx : %s", vmStartAddress, driver.errorMessage);
				continue;
			}

			if (useAdvancedSearch) {

				// pattern: 
				// 4c 8b d1 b8 00   00 00 00 ...  << call 0           ---
				// ...                                                 |
				// 4c 8b d1 b8 ??+0 00 00 00 ...  << found here        4
				// ...                                                 |
				// 4c 8b d1 b8 ??+x 00 00 00 ...  << to be patch     pages
				// ...                                                 |
				// 4c 8b d1 b8 40   00 00 00 ...  << call 40          ---
				// 
				// [call 40] by now, all call numbers we need are in range [0x0, 0x40].
				
				char pattern[] = "\x4c\x8b\xd1\xb8\x00\x00\x00\x00";
				for (LONG offset = 0; offset < 0x4000 - 0x20 * 0x41; offset++) {

					if (0 == memcmp(pattern, vmbuf + offset, 4) && 
						0 == memcmp(pattern + 5, vmbuf + offset + 5, 3)) {

						// possible call page, find call 0x40 to ensure pages not too short to write.
						// [caution] win7 map 0x10 bytes for each syscall stub.

						LONG syscall_num = *(LONG*)((ULONG64)vmbuf + offset + 0x4);
						LONG syscall_40_num = 0;

						if (osVersion == OSVersion::WIN_10_11) {
							syscall_40_num = *(LONG*)((ULONG64)vmbuf + offset - 0x20 * (LONG64)syscall_num + 0x20 * 0x40 + 0x4);
						} else {
							syscall_40_num = *(LONG*)((ULONG64)vmbuf + offset - 0x10 * (LONG64)syscall_num + 0x10 * 0x40 + 0x4);
						}

						if (syscall_40_num == 0x40) {

							// now we have ensured pages contain enough information. locate offset0 to syscall 0.
							if (osVersion == OSVersion::WIN_10_11) {
								offset0 = offset - 0x20 * syscall_num;
							} else {
								offset0 = offset - 0x10 * syscall_num;
							}

							systemMgr.log("patch_ntdll(): offset0 found at +0x%x (from: syscall 0x%x)", offset0, syscall_num);
							break;
						}
					}
				}

			} else {

				// syscall traits: 4c 8b d1 b8 ?? 00 00 00
				for (LONG offset = (0x1000 + *rip % 0x1000) - 0x14; /* rva from current syscall begin */
					offset < 0x4000 - 0x20 /* buf sz - bytes to write */; offset++) {
					if (vmbuf[offset] == '\x4c' &&
						vmbuf[offset + 1] == '\x8b' &&
						vmbuf[offset + 2] == '\xd1' &&
						vmbuf[offset + 3] == '\xb8' &&
						/* vmbuf[offset + 4] == ?? && */
						vmbuf[offset + 5] == '\x00' &&
						vmbuf[offset + 6] == '\x00' &&
						vmbuf[offset + 7] == '\x00') {

						// locate offset0 to syscall 0.
						LONG syscall_num = *(LONG*)((ULONG64)vmbuf + offset + 0x4);
						if (osVersion == OSVersion::WIN_10_11) {
							offset0 = offset - 0x20 * syscall_num;
						} else {
							offset0 = offset - 0x10 * syscall_num;
						}

						systemMgr.log("patch_ntdll(): offset0 found at +0x%x (from: syscall 0x%x)", offset0, syscall_num);
						break;
					}
				}
			}

			if (offset0 < 0 /* offset0 == -1: not found || offset0 < 0: out of page range */) {
				//systemMgr.log("patch_ntdll(): trait not found from %%rip/block = %llx", *rip);
				continue;
			} else {
				systemMgr.log("patch_ntdll(): trait found from %%rip/block = %llx", *rip);
				break;
			}
		}

		// check if offset0 found in all results.
		if (offset0 < 0) {
			systemMgr.log("patch_ntdll(): round %u: trait not found in all rips/blocks.", try_times);
			Sleep(5000);
			continue;
		} else {
			break;
		}
	}


	// decide whether trait found success in all rounds.
	// in case patch_stage1() success / fail, inc failcount / xor failcount.
	if (offset0 < 0) {
		patchFailCount ++;
		systemMgr.log("patch_ntdll(): search failed too many times, abort. (retry: %d)", patchFailCount.load());
		return false;
	} else {
		patchFailCount = 0;
	}


	// before manip memory, check process status.
	if (!(patchEnabled && pid == threadMgr.getTargetPid())) {
		systemMgr.log("patch_ntdll(): usr switched mode or process terminated, quit.");
		return false;
	}

	// assert: vmbuf is syscall pages && offset0 >= 0.
	// suspend target, then detour ntdll.

	driver.suspend(pid);

	DWORD callNum_delay = syscallTable["NtDelayExecution"];

	if (osVersion == OSVersion::WIN_10_11 && osBuildNum >= 10586) {

		// for win10 there're 0x20 bytes to place shellcode.
		// [22.9.16] rebuild trampoline, add context relocation.

		char patch_bytes[] = "\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
		/*
			0:  48 b8 00 00 00 00 00    movabs rax, <AllocAddress>
			7:  00 00 00
			a:  ff e0                   jmp    rax
		*/

		if (switches.NtQueryVirtualMemory) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x23\x00\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 23 00 00 00          mov    eax, 0x23
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 34 00 00 00          mov    eax, 0x34
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtQueryVirtualMemory"];
			LONG  offset        = offset0 + 0x20 /* win10 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[0].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);
			
			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtQueryVirtualMemory = true;

			// �ɰ�ʹ���������shellcode����ż��������SGUARD��������ָ���쳣����
			// �Ʋ�ԭ��Ϊsleepϵͳ�����޸��˵�����ĳ�����Ż����Ĵ����ľֲ�������
			// ���üĴ�����ԭϵͳ�����б��Ż���������Ϊ�����޸ģ���ntdll��װ��native api��Ϊ�����޸ģ�
			// �򲢷��ɱ������߱���ļĴ�������windows x64�������£���
			//	mov r10, rcx
			//	mov eax, 0x23
			//	syscall
			//	mov r10, 0xFFFFFFFFFF4143E0
			//	push r10
			//	mov rdx, rsp
			//	mov eax, 0x34
			//	syscall
			//	pop r10
			//	ret
		}

		if (switches.NtReadVirtualMemory) {

			char working_bytes[] = "\xB8\x22\x00\x00\xC0\xC3\x49\x89\xCA\xB8\x3F\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  b8 22 00 00 c0          mov    eax, 0xc0000022
				5:  c3                      ret
				6:  49 89 ca                mov    r10, rcx
				9:  b8 3f 00 00 00          mov    eax, 0x3f
				e:  0f 05                   syscall
				10: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtReadVirtualMemory"];
			LONG  offset        = offset0 + 0x20 /* win10 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0xa, &callNum_this, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress + 6);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtReadVirtualMemory = true;
		}

		if (switches.NtWaitForSingleObject) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x04\x00\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 23 00 00 00          mov    eax, 0x4
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 34 00 00 00          mov    eax, 0x34
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtWaitForSingleObject"];
			LONG  offset        = offset0 + 0x20 /* win10 syscall align */ * callNum_this;
			
			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[2].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtWaitForSingleObject = true;
		}

		if (switches.NtDelayExecution) { /* deprecated; no context fix */

			char patch_bytes[] =
				"\x48\xC7\x02\xE0\x43\x41\xFF\xB8\x34\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  48 c7 02 e0 43 41 ff    mov    QWORD PTR [rdx], 0xffffffffff4143e0
				7:  b8 34 00 00 00          mov    eax, 0x34
				c:  0f 05                   syscall
				e:  c3                      ret
			*/

			// get syscall offset.
			LONG  offset = offset0 + 0x20 /* win10 syscall align */ * callNum_delay;

			// patch_bytes:  add callNum_delay & delay.
			memcpy(patch_bytes + 0x8, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[3].load();
			memcpy(patch_bytes + 0x3, &delay_param, 4);

			// patch_bytes:  move to memory.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtDelayExecution = true;
		}

		if (switches.DeviceIoControl_1) { // NtDeviceIoControlFile

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtDeviceIoControlFile"];
			LONG  offset        = offset0 + 0x20 /* win10 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);


			if (switches.DeviceIoControl_1x) { // weaken enable

				char working_bytes[] =
					"\x8B\x44\x24\x30\x3D\x2C\x1C\x22\x00\x74\x09\x3D\x24\x1C\x22\x00\x74\x02\xEB\x45"
					"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
					"\x49\xC7\xC2\x40\x1E\x1B\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
					"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58"
					"\x49\x89\xCA\xB8\x07\x00\x00\x00\x0F\x05\xC3";
				/*
					0:  8b 44 24 30             mov    eax, DWORD PTR [rsp+0x30]
					4:  3d 2c 1c 22 00          cmp    eax, 0x221c2c
					9:  74 09                   je     14 <L1>
					b:  3d 24 1c 22 00          cmp    eax, 0x221c24
					10: 74 02                   je     14 <L1>
					12: eb 45                   jmp    59 <L2>
					0000000000000014 <L1>:
					14: 50                      push   rax
					15: 53                      push   rbx
					16: 51                      push   rcx
					17: 52                      push   rdx
					18: 56                      push   rsi
					19: 57                      push   rdi
					1a: 55                      push   rbp
					1b: 41 50                   push   r8
					1d: 41 51                   push   r9
					1f: 41 52                   push   r10
					21: 41 53                   push   r11
					23: 41 54                   push   r12
					25: 41 55                   push   r13
					27: 41 56                   push   r14
					29: 41 57                   push   r15
					2b: 9c                      pushf
					2c: 49 c7 c2 40 1e 1b ff    mov    r10, 0xffffffffff1b1e40
					33: 41 52                   push   r10
					35: 48 89 e2                mov    rdx, rsp
					38: b8 34 00 00 00          mov    eax, 0x34
					3d: 0f 05                   syscall
					3f: 41 5a                   pop    r10
					41: 9d                      popf
					42: 41 5f                   pop    r15
					44: 41 5e                   pop    r14
					46: 41 5d                   pop    r13
					48: 41 5c                   pop    r12
					4a: 41 5b                   pop    r11
					4c: 41 5a                   pop    r10
					4e: 41 59                   pop    r9
					50: 41 58                   pop    r8
					52: 5d                      pop    rbp
					53: 5f                      pop    rdi
					54: 5e                      pop    rsi
					55: 5a                      pop    rdx
					56: 59                      pop    rcx
					57: 5b                      pop    rbx
					58: 58                      pop    rax
					0000000000000059 <L2>:
					59: 49 89 ca                mov    r10, rcx
					5c: b8 07 00 00 00          mov    eax, 0x7
					61: 0f 05                   syscall
					63: c3                      ret
				*/

				// working_bytes:  add callNum_this.
				memcpy(working_bytes + 0x5d, &callNum_this, 4);

				// working_bytes:  add callNum_delay & delay.
				memcpy(working_bytes + 0x39, &callNum_delay, 4);
				LONG64 delay_param = (LONG64)-10000 * patchDelay[4].load();
				memcpy(working_bytes + 0x2f, &delay_param, 4);

				// working_bytes:  move to memory.
				memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
				status = driver.writeVM(pid, vmalloc, allocAddress);
				if (!status) {
					systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
					return false;
				}

				// try fix target context.
				_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
					(ULONG64)allocAddress + 0x59);

				// mark related flag to inform user that patch has complete.
				patchedNow.DeviceIoControl_1x = true;

			} else { // default intercept

				char working_bytes[] =
					"\x8B\x44\x24\x30\x3D\x2C\x1C\x22\x00\x75\x11\x48\x8B\x44\x24\x28\x48\xC7\x40\x08\x34\x00\x00\x00\x48\x31\xC0\xC3"
					"\x3D\x24\x1C\x22\x00\x75\x11\x48\x8B\x44\x24\x28\x48\xC7\x40\x08\x30\x08\x00\x00\x48\x31\xC0\xC3"
					"\x49\x89\xCA\xB8\x07\x00\x00\x00\x0F\x05\xC3";
				/*
					0:  8b 44 24 30             mov    eax, dword ptr [rsp+0x30]
					4:  3d 2c 1c 22 00          cmp    eax, 0x221c2c
					9:  75 11                   jne    0x1c
					b:  48 8b 44 24 28          mov    rax, qword ptr [rsp+0x28]
					10: 48 c7 40 08 34 00 00    mov    qword ptr [rax+0x8], 0x34
					17: 00
					18: 48 31 c0                xor    rax, rax
					1b: c3                      ret
					1c: 3d 24 1c 22 00          cmp    eax, 0x221c24
					21: 75 11                   jne    0x34
					23: 48 8b 44 24 28          mov    rax, qword ptr [rsp+0x28]
					28: 48 c7 40 08 30 08 00    mov    qword ptr [rax+0x8], 0x830
					2f: 00
					30: 48 31 c0                xor    rax, rax
					33: c3                      ret
					34: 49 89 ca                mov    r10, rcx
					37: b8 07 00 00 00          mov    eax, 0x7
					3c: 0f 05                   syscall
					3e: c3                      ret
				*/

				/*
				pseudocode:
					NTSTATUS fake_device(...) {
						if (IoCode == 0x221c2c) { // (ACE-BASE.sys) MDL_1 entry
							// construct dst header from src header ...
							pIoStatusBlock->Information = 0x34;
							return STATUS_SUCCESS; // 0x0
						}
						if (IoCode == 0x221c24) { // (ACE-BASE.sys) MDL_2 entry
							// construct dst header from src header ...
							pIoStatusBlock->Information = 0x830;
							return STATUS_SUCCESS;
						}
						return original_device(...);
					}
				*/

				// working_bytes:  add callNum_this.
				memcpy(working_bytes + 0x38, &callNum_this, 4);

				// working_bytes:  move to memory.
				memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
				status = driver.writeVM(pid, vmalloc, allocAddress);
				if (!status) {
					driver.resume(pid);
					systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
					return false;
				}

				// try fix target context.
				_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
					(ULONG64)allocAddress + 0x34);
			}

			// mark related flag to inform user that patch has complete.
			patchedNow.DeviceIoControl_1 = true;
		}

		if (switches.DeviceIoControl_2) { // NtFsControlFile

			char working_bytes[] =
				"\x8B\x44\x24\x30\x3D\xF4\x00\x09\x00\x75\x06\xB8\x01\x00\x00\xC0\xC3"
				"\x3D\xBB\x00\x09\x00\x75\x06\xB8\x01\x00\x00\xC0\xC3"
				"\x49\x89\xCA\xB8\x39\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  8b 44 24 30             mov    eax, dword ptr [rsp+0x30]
				4:  3d f4 00 09 00          cmp    eax, 0x900f4
				9:  75 06                   jne    0x11
				b:  b8 01 00 00 c0          mov    eax, 0xc0000001
				10: c3                      ret
				11: 3d bb 00 09 00          cmp    eax, 0x900bb
				16: 75 06                   jne    0x1e
				18: b8 01 00 00 c0          mov    eax, 0xc0000001
				1d: c3                      ret
				1e: 49 89 ca                mov    r10, rcx
				21: b8 39 00 00 00          mov    eax, 0x39
				26: 0f 05                   syscall
				28: c3                      ret
			*/

			/*
			pseudocode:
				NTSTATUS fake_ntfs(...) {
					if (IoCode == FSCTL_QUERY_USN_JOURNAL || IoCode == FSCTL_READ_USN_JOURNAL) {
						return STATUS_UNSUCCESSFUL;
					}
					return original_ntfs(...);
				}
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtFsControlFile"];
			LONG  offset        = offset0 + 0x20 /* win10 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x22, &callNum_this, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress + 0x1e);

			// mark related flag to inform user that patch has complete.
			patchedNow.DeviceIoControl_2 = true;
		}

	} else { // if WIN_7 / WIN_8 / WIN_81 / WIN_10_10240

		// NT 6.x / NT 10.0.10240: ntdll maps 0x10 bytes for each syscall (no jnz -> int 2E).
		// fortunately, by assigning param@ZeroBits we can make NtAllocateVirtualMemory gives an addr smaller than 0x1 0000 0000, then
		// 0x0: use mov eax instead of rax. (rax's high 32-bit is 0 due to x86_64 isa convention)
		// 0xa: [22.9.16 enhanced] nothing needs to be remained.

		char patch_bytes[] = "\xB8\x00\x00\x00\x00\xFF\xE0";
		/*
			0:  b8 00 00 00 00          mov    eax, <AllocAddress>
			5:  ff e0                   jmp    rax
		*/

		if (switches.NtQueryVirtualMemory) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x20\x00\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x31\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 20 00 00 00          mov    eax, 0x20
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 31 00 00 00          mov    eax, 0x31
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtQueryVirtualMemory"];
			LONG  offset        = offset0 + 0x10 /* win7 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[0].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtQueryVirtualMemory = true;
		}

		if (switches.NtReadVirtualMemory) {

			char working_bytes[] = "\xB8\x22\x00\x00\xC0\xC3\x49\x89\xCA\xB8\x3C\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  b8 22 00 00 c0          mov    eax, 0xc0000022
				5:  c3                      ret
				6:  49 89 ca                mov    r10, rcx
				9:  b8 3c 00 00 00          mov    eax, 0x3c
				e:  0f 05                   syscall
				10: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtReadVirtualMemory"];
			LONG  offset        = offset0 + 0x10 /* win7 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0xa, &callNum_this, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress + 6);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtReadVirtualMemory = true;
		}

		if (switches.NtWaitForSingleObject) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x20\x00\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x31\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 20 00 00 00          mov    eax, 0x20
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 31 00 00 00          mov    eax, 0x31
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtWaitForSingleObject"];
			LONG  offset        = offset0 + 0x10 /* win7 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[2].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtWaitForSingleObject = true;
		}

		if (switches.NtDelayExecution) { /* deprecated no context fix */

			char patch_bytes[] =
				"\x48\xC7\x02\xE0\x43\x41\xFF\xB8\x31\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  48 c7 02 e0 43 41 ff    mov    QWORD PTR [rdx], 0xffffffffff4143e0
				7:  b8 31 00 00 00          mov    eax, 0x34
				c:  0f 05                   syscall
				e:  c3                      ret
			*/

			// get syscall offset.
			LONG  offset = offset0 + 0x20 /* win10 syscall align */ * callNum_delay;

			// patch_bytes:  add callNum_delay & delay.
			memcpy(patch_bytes + 0x8, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[3].load();
			memcpy(patch_bytes + 0x3, &delay_param, 4);

			// patch_bytes:  move to memory.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// mark related flag to inform user that patch has complete.
			patchedNow.NtDelayExecution = true;
		}

		if (switches.DeviceIoControl_1) { // NtDeviceIoControlFile
			
			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtDeviceIoControlFile"];
			LONG  offset        = offset0 + 0x10 /* win7 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);


			if (switches.DeviceIoControl_1x) {

				char working_bytes[] =
					"\x8B\x44\x24\x30\x3D\x2C\x1C\x22\x00\x74\x09\x3D\x24\x1C\x22\x00\x74\x02\xEB\x45"
					"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
					"\x49\xC7\xC2\x40\x1E\x1B\xFF\x41\x52\x48\x89\xE2\xB8\x31\x00\x00\x00\x0F\x05\x41\x5A"
					"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58"
					"\x49\x89\xCA\xB8\x04\x00\x00\x00\x0F\x05\xC3";
				/*
					0:  8b 44 24 30             mov    eax, DWORD PTR [rsp+0x30]
					4:  3d 2c 1c 22 00          cmp    eax, 0x221c2c
					9:  74 09                   je     14 <L1>
					b:  3d 24 1c 22 00          cmp    eax, 0x221c24
					10: 74 02                   je     14 <L1>
					12: eb 45                   jmp    59 <L2>
					0000000000000014 <L1>:
					14: 50                      push   rax
					15: 53                      push   rbx
					16: 51                      push   rcx
					17: 52                      push   rdx
					18: 56                      push   rsi
					19: 57                      push   rdi
					1a: 55                      push   rbp
					1b: 41 50                   push   r8
					1d: 41 51                   push   r9
					1f: 41 52                   push   r10
					21: 41 53                   push   r11
					23: 41 54                   push   r12
					25: 41 55                   push   r13
					27: 41 56                   push   r14
					29: 41 57                   push   r15
					2b: 9c                      pushf
					2c: 49 c7 c2 40 1e 1b ff    mov    r10, 0xffffffffff1b1e40
					33: 41 52                   push   r10
					35: 48 89 e2                mov    rdx, rsp
					38: b8 31 00 00 00          mov    eax, 0x31
					3d: 0f 05                   syscall
					3f: 41 5a                   pop    r10
					41: 9d                      popf
					42: 41 5f                   pop    r15
					44: 41 5e                   pop    r14
					46: 41 5d                   pop    r13
					48: 41 5c                   pop    r12
					4a: 41 5b                   pop    r11
					4c: 41 5a                   pop    r10
					4e: 41 59                   pop    r9
					50: 41 58                   pop    r8
					52: 5d                      pop    rbp
					53: 5f                      pop    rdi
					54: 5e                      pop    rsi
					55: 5a                      pop    rdx
					56: 59                      pop    rcx
					57: 5b                      pop    rbx
					58: 58                      pop    rax
					0000000000000059 <L2>:
					59: 49 89 ca                mov    r10, rcx
					5c: b8 04 00 00 00          mov    eax, 0x4
					61: 0f 05                   syscall
					63: c3                      ret
				*/

				// working_bytes:  add callNum_this.
				memcpy(working_bytes + 0x5d, &callNum_this, 4);

				// working_bytes:  add callNum_delay & delay.
				memcpy(working_bytes + 0x39, &callNum_delay, 4);
				LONG64 delay_param = (LONG64)-10000 * patchDelay[4].load();
				memcpy(working_bytes + 0x2f, &delay_param, 4);

				// working_bytes:  move to memory.
				memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
				status = driver.writeVM(pid, vmalloc, allocAddress);
				if (!status) {
					systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
					return false;
				}

				// try fix target context.
				_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
					(ULONG64)allocAddress + 0x59);

				// mark related flag to inform user that patch has complete.
				patchedNow.DeviceIoControl_1x = true;

			} else {

				char working_bytes[] =
					"\x8B\x44\x24\x30\x3D\x2C\x1C\x22\x00\x75\x11\x48\x8B\x44\x24\x28\x48\xC7\x40\x08\x34\x00\x00\x00\x48\x31\xC0\xC3"
					"\x3D\x24\x1C\x22\x00\x75\x11\x48\x8B\x44\x24\x28\x48\xC7\x40\x08\x30\x08\x00\x00\x48\x31\xC0\xC3"
					"\x49\x89\xCA\xB8\x04\x00\x00\x00\x0F\x05\xC3";
				/*
					0:  8b 44 24 30             mov    eax, dword ptr [rsp+0x30]
					4:  3d 2c 1c 22 00          cmp    eax, 0x221c2c
					9:  75 11                   jne    0x1c
					b:  48 8b 44 24 28          mov    rax, qword ptr [rsp+0x28]
					10: 48 c7 40 08 34 00 00    mov    qword ptr [rax+0x8], 0x34
					17: 00
					18: 48 31 c0                xor    rax, rax
					1b: c3                      ret
					1c: 3d 24 1c 22 00          cmp    eax, 0x221c24
					21: 75 11                   jne    0x34
					23: 48 8b 44 24 28          mov    rax, qword ptr [rsp+0x28]
					28: 48 c7 40 08 30 08 00    mov    qword ptr [rax+0x8], 0x830
					2f: 00
					30: 48 31 c0                xor    rax, rax
					33: c3                      ret
					34: 49 89 ca                mov    r10, rcx
					37: b8 04 00 00 00          mov    eax, 0x4
					3c: 0f 05                   syscall
					3e: c3                      ret
				*/

				// working_bytes:  add callNum_this.
				memcpy(working_bytes + 0x38, &callNum_this, 4);

				// working_bytes:  move to memory.
				memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
				status = driver.writeVM(pid, vmalloc, allocAddress);
				if (!status) {
					driver.resume(pid);
					systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
					return false;
				}

				// try fix target context.
				_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
					(ULONG64)allocAddress + 0x34);
			}

			// mark related flag to inform user that patch has complete.
			patchedNow.DeviceIoControl_1 = true;
		}

		if (switches.DeviceIoControl_2) { // NtFsControlFile

			char working_bytes[] =
				"\x8B\x44\x24\x30\x3D\xF4\x00\x09\x00\x75\x06\xB8\x01\x00\x00\xC0\xC3"
				"\x3D\xBB\x00\x09\x00\x75\x06\xB8\x01\x00\x00\xC0\xC3"
				"\x49\x89\xCA\xB8\x36\x00\x00\x00\x0F\x05\xC3";
			/*
				0:  8b 44 24 30             mov    eax, dword ptr [rsp+0x30]
				4:  3d f4 00 09 00          cmp    eax, 0x900f4
				9:  75 06                   jne    0x11
				b:  b8 01 00 00 c0          mov    eax, 0xc0000001
				10: c3                      ret
				11: 3d bb 00 09 00          cmp    eax, 0x900bb
				16: 75 06                   jne    0x1e
				18: b8 01 00 00 c0          mov    eax, 0xc0000001
				1d: c3                      ret
				1e: 49 89 ca                mov    r10, rcx
				21: b8 36 00 00 00          mov    eax, 0x36
				26: 0f 05                   syscall
				28: c3                      ret
			*/

			// get syscall num & offset.
			DWORD callNum_this  = syscallTable["NtFsControlFile"];
			LONG  offset        = offset0 + 0x10 /* win7 syscall align */ * callNum_this;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x22, &callNum_this, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + offset, vmStartAddress + offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress + 0x1e);

			// mark related flag to inform user that patch has complete.
			patchedNow.DeviceIoControl_2 = true;
		}
	}


	// write memory.
	status =
	driver.writeVM(pid, vmbuf, (PVOID)vmStartAddress);
	if (!status) {
		driver.resume(pid);
		systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
		return false;
	}

	// clear cpu instruction cache.
	auto hProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, pid);
	if (hProc) {
		if (!FlushInstructionCache(hProc, (PVOID)vmStartAddress, 0x4000)) {
			systemMgr.log("patch_ntdll(): FlushInstructionCache() failed.");
		}
		CloseHandle(hProc);
	}

	// execute target in new state.
	driver.resume(pid);


	// stage1 complete.
	if (patchedNow.NtQueryVirtualMemory)   patchStatus.NtQueryVirtualMemory   = true;
	if (patchedNow.NtReadVirtualMemory)    patchStatus.NtReadVirtualMemory    = true;
	if (patchedNow.NtWaitForSingleObject)  patchStatus.NtWaitForSingleObject  = true;
	if (patchedNow.NtDelayExecution)       patchStatus.NtDelayExecution       = true;
	if (patchedNow.DeviceIoControl_1)      patchStatus.DeviceIoControl_1      = true;
	if (patchedNow.DeviceIoControl_1x)     patchStatus.DeviceIoControl_1x     = true;
	if (patchedNow.DeviceIoControl_2)      patchStatus.DeviceIoControl_2      = true;

	systemMgr.log("patch_ntdll(): patch complete.");
	return true;
}

bool PatchManager::_patch_user32(DWORD pid, patchSwitches_t& switches) {

	win32ThreadManager       threadMgr;
	ULONG64                  vmStartAddress    = NULL;
	auto                     vmbuf_ptr         = std::make_unique<char[]>(0x4000);
	auto                     vmalloc_ptr       = std::make_unique<char[]>(0x4000);

	auto                     osVersion         = systemMgr.getSystemVersion();
	auto                     osBuildNum        = systemMgr.getSystemBuildNum();
	auto                     vmbuf             = vmbuf_ptr.get();
	auto                     vmalloc           = vmalloc_ptr.get();
	
	patchStatus_t            patchedNow;
	bool                     status;


	// assert: driver loaded.
	systemMgr.log("patch_user32(): entering.");

	// if using normal search, wait for stable.
	for (auto time = 0; !useAdvancedSearch && time < 5; time++) {
		Sleep(1000);
		if (!patchEnabled || pid != threadMgr.getTargetPid()) {
			systemMgr.log("patch_user32(): primary wait: pid not match or patch disabled, quit.");
			return false;
		}
	}


	// find win32u / user32 target offset.
	// target_offset: target syscall entry, from vmbuf:0.
	LONG target_offset = -1;

	// try multi-times to find target offset.
	for (auto try_times = 1; try_times <= 3; try_times++) {
		
		std::vector<ULONG64> rips;
		rips.clear();

		if (useAdvancedSearch) {
			systemMgr.log("patch_user32(): [Caution] using Advanced memory search.");

			// search memory executable modules in given image from kernel structs.
			std::vector<ULONG64> executeRange;
			
			status = 
			driver.searchVad(pid, executeRange, L"User32.dll");
			
			if (!status) {
				systemMgr.panic(driver.errorCode, "patch_user32(): �ڴ�ɨ��ʧ��: %s", driver.errorMessage);
			}

			// check if result exists.
			if (executeRange.empty()) {
				systemMgr.panic("patch_user32(): �޷���Ŀ��������ҵ�ģ�顰User32��");
			}

			// split executable range to pieces to read.
			for (size_t i = 0; i < executeRange.size(); i += 2) {

				auto moduleVABegin  = executeRange[i];
				auto moduleVAEnd    = executeRange[i + 1];

				for (auto moduleVA = moduleVABegin + 0x1000; moduleVA < moduleVAEnd; moduleVA += 0x4000) {
					rips.push_back(moduleVA);
				}
			}

		} else {
			systemMgr.log("patch_user32(): using normal rip search.");
			
			// get potential rip in top 3 threads like before.
			rips = _findRip(true);
		}

		if (rips.empty()) {
			systemMgr.log("patch_user32(): rips empty, quit.");
			return false;
		}


		// find offset we need in all rip given, load memory to vmbuf btw.
		for (auto rip = rips.begin(); rip != rips.end(); ++rip) {

			// round page.
			vmStartAddress = (*rip & ~0xfff) - 0x1000;


			// read memory.
			status =
			driver.readVM(pid, vmbuf, (PVOID)vmStartAddress);

			if (!status) {
				systemMgr.log(driver.errorCode, "patch_user32() warning: load memory failed at 0x%llx : %s", vmStartAddress, driver.errorMessage);
				continue;
			}


			// get mem trait.
			char traits[] = "\x4c\x8b\xd1\xb8\x44\x10\x00\x00";
			*(DWORD*)(traits + 4) = syscallTable["NtUserGetAsyncKeyState"];

			// loop var to search for syscall entry.
			LONG offset;

			// search round 1: find exact value.
			offset = 0x1000 + *rip % 0x1000 - 0x14;
			for (offset < 0 ? offset = 0 : 0; offset <= 0x4000 - 0x20; offset++) {
				if (0 == memcmp(vmbuf + offset, traits, 8)) { // if strict: 4c 8b d1 b8 XX 10 00 00

					target_offset = offset;

					systemMgr.log("patch_user32(): strict search: target_offset found at +0x%x (syscall 0x%x)", target_offset, *(LONG*)(traits + 4));
					break;
				}
			}

			// search round 2: if not found, find fuzzy value. (WIN_10_11 only)
			// [remark] WIN_7 do NOT map continuous memory for syscall 0x1xxx by a single library (win32u.dll).
			// syscalls are simply inlined in user32.dll's function implementation.
			// just like ntdll, win32u are all aligned to 0x20 Bytes in WIN_10_11.
			if (target_offset == -1 && osVersion == OSVersion::WIN_10_11) {

				for (offset = 0x0; offset <= 0x4000 - 0x20; offset++) {
					if (0 == memcmp(vmbuf + offset, traits, 4) &&
						0 == memcmp(vmbuf + offset + 5, traits + 5, 3)) { // if fuzzy: 4c 8b d1 b8 ?? 10 00 00

						// from the syscall we found, switch to syscall 0x1000, then switch to syscall we need.
						LONG found_call_num = *(LONG*)(vmbuf + offset + 4);
						LONG real_call_num = *(LONG*)(traits + 4);
						target_offset = offset - (found_call_num - 0x1000) * 0x20 + (real_call_num - 0x1000) * 0x20;

						systemMgr.log("patch_user32(): fuzzy search: target_offset found at +0x%x (from syscall 0x%x)", target_offset, found_call_num);
						break;
					}
				}
			}

			// search round 3: if not found, find relative in user32 and jump over. (WIN_10_11 only)
			// [remark] WIN_7 do NOT use rex.W call to enter syscall part.
			// however, WIN_10_11 use them to enter win32u.dll. In other case, this prefix is less likely to use.
			// (21.12.10) enhanced: search is capable for any reference to user32.dll.
			if (target_offset == -1 && osVersion == OSVersion::WIN_10_11) {

				char user32_traits[] = "\x48\xFF\x15\x00\x00\x00\x00";
				/*
					traits: user32.dll!AnyTrapFunction+0x??:
					0:  48 ff 15 ?? ?? ?? ??    rex.W call qword ptr [rip+<imm32>]  ; <__imp_NtUserXxx...> 
					7:  ??                      (possibly) nop dword ptr ...        ; if it was a trap entry
				*/

				int fake_entries = 0;

				for (offset = 0x0; offset < 0x4000 - 0x20; offset++) {

					if (0 == memcmp(vmbuf + offset, user32_traits, 3)) {

						auto vmrelate_ptr = std::make_unique<char[]>(0x4000);
						auto vmrelate = vmrelate_ptr.get();

						// parse instruction: rex.w call.
						auto relative_shift  = *(LONG*)(vmbuf + offset + 0x3);
						auto vaddress_ptr    = vmStartAddress + offset + 0x7 + relative_shift;

						if (!driver.readVM(pid, vmrelate, (PVOID)((vaddress_ptr & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch_user32(): read *vaddress_ptr (%llx) failed.", vaddress_ptr);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}
						
						// since rex.w call -> vaddress_entry, load memory vaddress_entry points to.
						auto vaddress_entry = *(ULONG64*)(vmrelate + 0x1000 + vaddress_ptr % 0x1000);

						if (!driver.readVM(pid, vmrelate, (PVOID)((vaddress_entry & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch_user32(): load pages at vaddress_entry = 0x%llx failed.", vaddress_entry);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}

						// check if vaddress_entry points to valid related system call:
						// traits should match and call_number_found should be in range [0x1000, 0x1fff].
						auto call_number_found = *(LONG*)(vmrelate + 0x1000 + vaddress_entry % 0x1000 + 4);

						if (!(0 == memcmp(vmrelate + 0x1000 + vaddress_entry % 0x1000, traits, 4) &&
							call_number_found >= 0x1000 && call_number_found <= 0x1fff)) {
							fake_entries ++;
							continue;
						}

						// now we have ensured vaddress_entry -> __imp_NtUserXxx... .
						// use call_number_target to shift vaddress_entry to target_entry,
						auto call_number_target = *(LONG*)(traits + 4);
						auto target_entry       = vaddress_entry - (LONG64)call_number_found * 0x20 + (LONG64)call_number_target * 0x20;
						
						// switch to target_entry memory, 
						if (!driver.readVM(pid, vmrelate, (PVOID)((target_entry & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch_user32(): switch win32u pages to vaddress_entry = 0x%llx failed.", vaddress_entry);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}
						
						// then modify final result.
						// no need to check target traits again; patch it directly. 
						// only ptrs're changed and kbs of memory're wasted since game didn't restart.
						target_offset   = 0x1000 + target_entry % 0x1000;
						vmStartAddress  = (target_entry & ~0xfff) - 0x1000;
						memcpy(vmbuf, vmrelate, 0x4000);


						systemMgr.log("patch_user32(): relative search: catched target_offset at +0x%llx, after %d fake traps.", target_offset, fake_entries);
						systemMgr.log("patch_user32():   >> search path: [rip+0x%x] ~ 0x%llx => 0x%llx (syscall 0x%x) => 0x%llx (syscall 0x%x)",
							relative_shift, vaddress_ptr, vaddress_entry, call_number_found, target_entry, call_number_target);
						
						break;
					}
				}
			}

			if (target_offset == -1) {
				//systemMgr.log("patch_user32(): trait not found from %%rip/block = %llx.", *rip);
				continue;
			} else {
				systemMgr.log("patch_user32(): trait found from %%rip/block = %llx.", *rip);
				break;
			}
		}

		// check if target_offset found in all result in _findRip().
		if (target_offset == -1) {
			systemMgr.log("patch_user32(): round %u: trait not found in all rips/blocks.", try_times);
			Sleep(5000);
			continue;
		} else {
			break;
		}
	}

	// decide whether trait found success in all rounds.
	if (target_offset == -1) {
		systemMgr.log("patch_user32(): no memory trait found in user32, abort.");
		return false;
	}


	// before manip memory, check process status.
	if (!(patchEnabled && pid == threadMgr.getTargetPid())) {
		systemMgr.log("patch_user32(): usr switched mode or process terminated, quit.");
		return false;
	}

	// assert: vmbuf is syscall pages && offset0 >= 0.
	// suspend target, then detour user32/win32u.

	driver.suspend(pid);

	DWORD callNum_delay = syscallTable["NtDelayExecution"];

	if (osVersion == OSVersion::WIN_10_11 && osBuildNum >= 10586) {

		char patch_bytes[] = "\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
		/*
			0:  48 b8 00 00 00 00 00    movabs rax, <AllocAddress>
			7:  00 00 00
			a:  ff e0                   jmp    rax
		*/

		if (switches.GetAsyncKeyState) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x44\x10\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 23 00 00 00          mov    eax, <call number>
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 34 00 00 00          mov    eax, 0x34
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset (target_offset).
			DWORD callNum_this = syscallTable["NtUserGetAsyncKeyState"];
			
			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.log(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x2, &allocAddress, 8);
			memcpy(vmbuf + target_offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[1].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + target_offset, vmStartAddress + target_offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.GetAsyncKeyState = true;
		}
		

	} else { // if WIN_7 / WIN_8 / WIN_81 / WIN_10_10240

		char patch_bytes[] = "\xB8\x00\x00\x00\x00\xFF\xE0";
		/*
			0:  b8 00 00 00 00          mov    eax, <AllocAddress>
			5:  ff e0                   jmp    rax
		*/

		if (switches.GetAsyncKeyState) {

			char working_bytes[] =
				"\x49\x89\xCA\xB8\x44\x10\x00\x00\x0F\x05"
				"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 23 00 00 00          mov    eax, <call number>
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  53                      push   rbx
				c:  51                      push   rcx
				d:  52                      push   rdx
				e:  56                      push   rsi
				f:  57                      push   rdi
				10: 55                      push   rbp
				11: 41 50                   push   r8
				13: 41 51                   push   r9
				15: 41 52                   push   r10
				17: 41 53                   push   r11
				19: 41 54                   push   r12
				1b: 41 55                   push   r13
				1d: 41 56                   push   r14
				1f: 41 57                   push   r15
				21: 9c                      pushf
				22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				29: 41 52                   push   r10
				2b: 48 89 e2                mov    rdx, rsp
				2e: b8 34 00 00 00          mov    eax, 0x34
				33: 0f 05                   syscall
				35: 41 5a                   pop    r10
				37: 9d                      popf
				38: 41 5f                   pop    r15
				3a: 41 5e                   pop    r14
				3c: 41 5d                   pop    r13
				3e: 41 5c                   pop    r12
				40: 41 5b                   pop    r11
				42: 41 5a                   pop    r10
				44: 41 59                   pop    r9
				46: 41 58                   pop    r8
				48: 5d                      pop    rbp
				49: 5f                      pop    rdi
				4a: 5e                      pop    rsi
				4b: 5a                      pop    rdx
				4c: 59                      pop    rcx
				4d: 5b                      pop    rbx
				4e: 58                      pop    rax
				4f: c3                      ret
			*/

			// get syscall num & offset (target_offset).
			DWORD callNum_this = syscallTable["NtUserGetAsyncKeyState"];

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.log(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// patch_bytes:  add allocAddress & move to memory.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);
			memcpy(vmbuf + target_offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes:  add callNum_this.
			memcpy(working_bytes + 0x4, &callNum_this, 4);

			// working_bytes:  add callNum_delay & delay.
			memcpy(working_bytes + 0x2f, &callNum_delay, 4);
			LONG64 delay_param = (LONG64)-10000 * patchDelay[1].load();
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// working_bytes:  move to memory.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				driver.resume(pid);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// try fix target context.
			_fixThreadContext(vmStartAddress + target_offset, vmStartAddress + target_offset + sizeof(patch_bytes) - 1,
				(ULONG64)allocAddress);

			// mark related flag to inform user that patch has complete.
			patchedNow.GetAsyncKeyState = true;
		}
	}


	// write memory.
	status =
	driver.writeVM(pid, vmbuf, (PVOID)vmStartAddress);
	if (!status) {
		driver.resume(pid);
		systemMgr.log(driver.errorCode, "patch_user32(): writeVM() failed : %s", driver.errorMessage);
		return false;
	}
	
	// clear cpu instruction cache.
	auto hProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, pid);
	if (hProc) {
		if (!FlushInstructionCache(hProc, (PVOID)vmStartAddress, 0x4000)) {
			systemMgr.log("patch_user32(): FlushInstructionCache() failed.");
		}
		CloseHandle(hProc);
	}

	// run target peacefully in its new era.
	driver.resume(pid);


	// stage2 complete.
	if (patchedNow.GetAsyncKeyState) patchStatus.GetAsyncKeyState = true;

	systemMgr.log("patch_user32(): patch complete.");
	return true;
}

bool PatchManager::_fixThreadContext(ULONG64 pOrgStart, ULONG64 pOrgEnd, ULONG64 pDetour) {

	// if some thread(s) are executing patching area, relocate it's Pc.
	// assert: driver loaded && target suspended.
	
	win32ThreadManager                  threadMgr;
	auto&                               threadList    = threadMgr.threadList;
	CONTEXT                             context;
	context.ContextFlags = CONTEXT_CONTROL;


	if (!threadMgr.getTargetPid()) {
		systemMgr.log("_fixThreadContext(): pid not found, quit.");
		return false;
	}

	if (!threadMgr.enumTargetThread()) {
		systemMgr.log("_fixThreadContext(): open thread failed, quit.");
		return false;
	}

	for (auto& thread : threadList) {

		if (GetThreadContext(thread.handle, &context)) {

			if (context.Rip >= pOrgStart && context.Rip <= pOrgEnd) {
				
				DWORD64 newPc;

				// assert: pDetour = [mov rcx, r10; mov eax, <imm32>; syscall;]
				// assert: NT 10.0.10586 and later, pOrgStart = [mov rax, <imm64>; jmp rax;] (0xb)
				/*
					+0:  mov r10, rcx
					+3:  mov eax, ????h
					+8:  test ... ; << relocate to [syscall].
					+10: jnz ...  ; << patch before this instruction. no relocate if running here.
					+12: syscall
					+14: ret
				*/
				// assert: NT6.x and NT 10.0.10240, pOrgStart = [mov eax, <imm32>; jmp rax;] (0x6)
				/*
					+0: mov r10, rcx
					+3: mov eax, ????h
					+8: syscall   ; << patch before this instruction. no relocate if running here.
					+A: ret
				*/

				// in summary:
				// if Pc is at [+0, +8]: move to pDetour+offset.
				// if Pc is after +8: only long inline (10586+), move to syscall.

				if (context.Rip - pOrgStart <= 8) {
					newPc = pDetour + (context.Rip - pOrgStart);
					systemMgr.log("_fixThreadContext(): thrd 0x%x: relocating Pc before trap: 0x%llx -> 0x%llx", thread.tid, context.Rip, newPc);

				} else {
					newPc = pDetour + 8;
					systemMgr.log("_fixThreadContext(): thrd 0x%x: relocating Pc to trap: 0x%llx -> 0x%llx", thread.tid, context.Rip, newPc);
				}
				
				context.Rip = newPc;
				SetThreadContext(thread.handle, &context);
			}
		}
	}

	return true;
}


std::vector<ULONG64>
PatchManager::_findRip(bool useAll) {

	win32ThreadManager                  threadMgr;
	auto&                               threadList   = threadMgr.threadList;
	constexpr auto                      sampleSecs   = 5;
	std::unordered_map<ULONG64, DWORD>  contextMap;  // rip -> visit times
	CONTEXT                             context;
	context.ContextFlags = CONTEXT_CONTROL;

	char                                logBuf      [0x1000];
	std::vector<ULONG64>                result       = {};
	

	// open thread.
	if (!threadMgr.getTargetPid()) {
		systemMgr.log("_findRip(): pid not found, quit.");
		return result;
	}

	if (!threadMgr.enumTargetThread()) {
		systemMgr.log("_findRip(): open thread failed, quit.");
		return result;
	}


	// find out most consuming threads.
	// 1. sample cycles for first time to get previous cycles, to calc delta.
	for (auto& item : threadList) {
		QueryThreadCycleTime(item.handle, &item.cycles);
	}
	// 2. sample cycles for sampleSecs secs.
	for (auto time = 1; time <= sampleSecs; time++) {
		Sleep(1000);
		for (auto& item : threadList) {
			ULONG64 cycles;
			QueryThreadCycleTime(item.handle, &cycles);
			item.cycleDelta = cycles - item.cycles;
			item.cycles = cycles;
			// put delta together here; then divide by secs.
			item.cycleDeltaAvg += item.cycleDelta;
		}
	}
	// calc avg cycledelta.
	for (auto& item : threadList) {
		item.cycleDeltaAvg /= sampleSecs;
	}


	// sort by cycleDeltaAvg in decending order.
	std::sort(threadList.begin(), threadList.end(),
		[](auto& a, auto& b) { return a.cycleDeltaAvg > b.cycleDeltaAvg; });

	strcpy(logBuf, "_findRip(): top 3 threads: ");
	for (auto i = 0; i < threadList.size() && i < 3; i++) {
		sprintf(logBuf + strlen(logBuf), " %u(%llu)", threadList[i].tid, threadList[i].cycleDeltaAvg);
	}
	systemMgr.log(logBuf);


	// sample rip in top 3 threads. 
	for (auto i = 0; i < threadList.size() && i < 3; i++) { // i: thread No.

		contextMap.clear();

		// sample 4 rounds. each round sample .5sec and wait .5sec.
		for (auto round = 1; round <= 4; round++) {
			for (auto time = 1; time <= 50; time++) {
				driver.suspend(threadMgr.pid); // warning: API SuspendThread is blocked.
				if (GetThreadContext(threadList[i].handle, &context)) {
					contextMap[context.Rip] ++;
				}
				driver.resume(threadMgr.pid);
				Sleep(10);
			}
			Sleep(500);
		}


		systemMgr.log("_findRip(): context of thread %u:", threadList[i].tid);

		// if sample complete successfully, record visited rip in each thread in decending order.
		if (!contextMap.empty()) {

			for (auto& it : contextMap)
				systemMgr.log("_findRip(): > rip %llx -> cnt %u", it.first, it.second);

			auto ripneed = useAll ? contextMap.size() : 5;
			for (auto ripcount = 0; !contextMap.empty() && ripcount < ripneed; ripcount++) {
				ULONG64 rip =
					std::max_element(
						contextMap.begin(), contextMap.end(),
						[](auto& a, auto& b) { return a.second < b.second; })
					->first;
				contextMap.erase(rip);
				result.push_back(rip);
			}
		} else {
			systemMgr.log("_findRip(): > (empty)");
		}
	}

	systemMgr.log("_findRip(): find result: vector to be returned contains %u elements.", result.size());
	
	return result;
}

void PatchManager::_outVmbuf(ULONG64 vmStart, const char* vmbuf) {  // unused: for dbg only

	char title[0x1000];
	time_t t = time(0);
	tm* local = localtime(&t);
	sprintf(title, "[%d-%02d-%02d %02d.%02d.%02d]",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
	sprintf(title + strlen(title), "at_%llx.txt", vmStart);

	FILE* fp = fopen(title, "w");
	if (fp) {
		for (auto i = 0; i < 0x4000; i++) {
			fprintf(fp, "%02X", (UCHAR)vmbuf[i]);
			if (i % 32 == 31) fprintf(fp, "\n");
		}
		fclose(fp);
	}
	else {
		systemMgr.panic("����Vmbuf�ļ�ʧ�ܡ�");
	}
}