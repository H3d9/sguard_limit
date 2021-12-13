// Memory Patch（用户态模块）
// 2021.10.4 雨
// 昨天吃坏肚子了，很疼。但是 2.2 复刻胡桃，开心。
// 2021.11.27 24:00
// 大城市的郊区有着明亮的月亮。明天的露水在墙上凝结。
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
extern volatile DWORD         g_Mode;      // xref: patch::init()


// mempatch module
PatchManager  PatchManager::patchManager;

PatchManager::PatchManager()
	: patchEnabled(true), patchPid(0), patchFailCount(), 
	  patchSwitches{}, patchStatus{}, patchDelay{},
	  patchDelayRange{
	   { 200, 1500, 2500 },   /* NtQueryVirtualMemory */
	   { 100, 1000, 1500 },   /* GetAsyncKeyState */
	   { 1,   10,   200  },   /* NtWaitForSingleObject */
	   { 500, 1250, 2000 }    /* NtDelayExecution */
	  }, vmStartAddress(0), vmbuf_ptr(new CHAR[0x4000]), vmalloc_ptr(new CHAR[0x4000]) {}

PatchManager& PatchManager::getInstance() {
	return patchManager;
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
		patchPid           = 0;
		patchStatus.stage1 = false;
		patchStatus.stage2 = false;


		// wait for stable as target just start up.
		for (auto time = 0; time < 10; time++) {

			if (!patchEnabled || pid != threadMgr.getTargetPid()) {
				systemMgr.log("patch(): primary wait: pid not match or patch disabled, quit.");
				return;
			}

			Sleep(1000);
		}


		// start driver.
		if (!driver.load()) {
			systemMgr.log(driver.errorCode, "patch().driver.load(): %s", driver.errorMessage);
			systemMgr.panic(driver.errorCode, "patch().driver.load(): %s", driver.errorMessage);
			return;
		}


		// v2 switches (ntdll).
		if (patchSwitches.NtQueryVirtualMemory  || 
			patchSwitches.NtWaitForSingleObject || 
			patchSwitches.NtDelayExecution
			) {
			if (_patch_stage1(pid)) {
				patchStatus.stage1 = true;
			} else {
				// if v2 fails, stop driver and quit (to retry).
				driver.unload();
				return;
			}
		}


		// v3 switches (user32).
		if (patchSwitches.GetAsyncKeyState) {
			if (_patch_stage2(pid)) {
				patchStatus.stage2 = true;
			}
		}


		// stop driver.
		driver.unload();

		patchPid = pid;
		systemMgr.log("patch(): operation complete.");
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

bool PatchManager::_patch_stage1(DWORD pid) {

	win32ThreadManager     threadMgr;
	auto                   osVersion         = systemMgr.getSystemVersion();
	auto                   vmbuf             = vmbuf_ptr.get();
	auto                   vmalloc           = vmalloc_ptr.get();
	bool                   status;


	// assert: driver loaded.
	systemMgr.log("patch1(): entering.");

	// before stable, wait a second.
	for (auto time = 0; time < 5; time++) {

		if (!patchEnabled || pid != threadMgr.getTargetPid()) {
			systemMgr.log("patch1(): primary wait: pid not match or patch disabled, quit.");
			return false;
		}

		Sleep(1000);
	}


	// find ntdll syscall 0 offset.
	// offset0: syscall entry rva from vmbuf:0.
	LONG offset0 = -1;

	// try multi-times to find target offset.
	for (auto try_times = 1; try_times <= 3; try_times++) {

		// get potential rip in top 3 threads.
		auto rips = _findRip();

		if (rips.empty()) {
			systemMgr.log("patch1(): rips empty, quit.");
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
				systemMgr.log(driver.errorCode, "patch1() warning: load memory failed at 0x%llx : %s", vmStartAddress, driver.errorMessage);
				continue;
			}


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

					systemMgr.log("patch1(): offset0 found at +0x%x (from: syscall 0x%x)", offset0, syscall_num);
					break;
				}
			}

			if (offset0 < 0 /* offset0 == -1: not found || offset0 < 0: out of page range */) {
				systemMgr.log("patch1(): trait not found at %%rip = %llx", *rip);
				continue;
			} else {
				systemMgr.log("patch1(): trait found at %%rip = %llx", *rip);
				break;
			}
		}

		// check if offset0 found in all result in _findRip().
		if (offset0 < 0) {
			systemMgr.log("patch1(): round %u: trait not found in all rips.", try_times);
			Sleep(15000);
			continue;
		} else {
			break;
		}
	}


	// decide whether trait found success in all rounds.
	// in case patch_stage1() success / fail, inc failcount / xor failcount.
	if (offset0 < 0) {
		patchFailCount ++;
		systemMgr.log("patch1(): search failed too many times, abort. (retry: %d)", patchFailCount);
		return false;
	} else {
		patchFailCount = 0;
	}


	// before manip memory, check process status.
	if (!(patchEnabled && pid == threadMgr.getTargetPid())) {
		systemMgr.log("patch1(): usr switched mode or process terminated, quit.");
		return false;
	}

	// assert: vmbuf is syscall pages && offset0 >= 0.
	// patch according to switches.
	if (osVersion == OSVersion::WIN_10_11) {

		// for win10 there're 0x20 bytes to place shellcode.

		if (patchSwitches.NtQueryVirtualMemory) { // 0x23

			CHAR patch_bytes[] = "\x49\x89\xCA\xB8\x23\x00\x00\x00\x0F\x05\x50\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0\x58\xC3";
			/*
				0:  49 89 ca                mov    r10, rcx
				3:  b8 23 00 00 00          mov    eax, 0x23
				8:  0f 05                   syscall
				a:  50                      push   rax
				b:  48 b8 00 00 00 00 00    movabs rax, <AllocAddress>
				12: 00 00 00
				15: ff e0                   jmp    rax
				17: 58                      pop    rax
				18: c3                      ret
			*/

			CHAR working_bytes[] =
				"\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B"
				"\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
			/*
				0:  53                      push   rbx
				1:  51                      push   rcx
				2:  52                      push   rdx
				3:  56                      push   rsi
				4:  57                      push   rdi
				5:  55                      push   rbp
				6:  41 50                   push   r8
				8:  41 51                   push   r9
				a:  41 52                   push   r10
				c:  41 53                   push   r11
				e:  41 54                   push   r12
				10: 41 55                   push   r13
				12: 41 56                   push   r14
				14: 41 57                   push   r15
				16: 9c                      pushf
				17: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				1e: 41 52                   push   r10
				20: 48 89 e2                mov    rdx, rsp
				23: b8 34 00 00 00          mov    eax, 0x34
				28: 0f 05                   syscall
				2a: 41 5a                   pop    r10
				2c: 9d                      popf
				2d: 41 5f                   pop    r15
				2f: 41 5e                   pop    r14
				31: 41 5d                   pop    r13
				33: 41 5c                   pop    r12
				35: 41 5b                   pop    r11
				37: 41 5a                   pop    r10
				39: 41 59                   pop    r9
				3b: 41 58                   pop    r8
				3d: 5d                      pop    rbp
				3e: 5f                      pop    rdi
				3f: 5e                      pop    rsi
				40: 5a                      pop    rdx
				41: 59                      pop    rcx
				42: 5b                      pop    rbx
				43: 48 b8 00 00 00 00 00    mov    rax, <returnAddress>
				4a: 00 00 00
				4d: ff e0                   jmp    rax
			*/

			// syscall rva => offset.
			LONG offset = offset0 + 0x20 /* win10 syscall align */ * 0x23;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): allocVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// allocAddress => patch_bytes.
			memcpy(patch_bytes + 0xd, &allocAddress, 8);

			// delay => working_bytes.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[0];
			memcpy(working_bytes + 0x1a, &delay_param, 4);

			// returnAddress => working_bytes.
			ULONG64	returnAddress = vmStartAddress + offset + 0x17;
			memcpy(working_bytes + 0x45, &returnAddress, 8);

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes => allocated vm.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// 旧版使用以下这段shellcode，这偶尔会引发SGUARD崩溃（空指针异常），
			// 推测原因为sleep系统调用修改了调用者某个被优化到寄存器的局部变量，
			// 而该寄存器在原系统调用中被优化编译器认为不会修改，或被ntdll封装的native api认为不会修改，
			// 或并非由被调用者保存的寄存器（在windows x64的语义下）。
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

		if (patchSwitches.NtWaitForSingleObject) { // 0x4

			CHAR patch_bytes[] = "\x50\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0\x58\xC3\x90\x90\x90\x90\x90\xC3";
			/*
				; for simplicity we assert there's no jinx thread between +0~+14 (otherwise target'll crash)
				; treat them as they're waiting at +0x14.

				0:  50                      push   rax
				1:  48 b8 00 00 00 00 00    movabs rax, <AllocAddress>
				8:  00 00 00
				b:  ff e0                   jmp    rax
				d:  58                      pop    rax
				e:  c3                      ret
				f:  90                      nop
				10: 90                      nop
				11: 90                      nop
				12: 90                      nop
				13: 90                      nop
				14: c3                      ret        ; previous syscall rip expected returning here.
			*/

			CHAR working_bytes[] =
				"\x58\x49\x89\xCA\xB8\x04\x00\x00\x00\x0F\x05\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
				"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B"
				"\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
			/*
				0:  58                      pop    rax
				1:  49 89 ca                mov    r10, rcx
				4:  b8 04 00 00 00          mov    eax, 0x4
				9:  0f 05                   syscall
				b:  50                      push   rax
				c:  53                      push   rbx
				d:  51                      push   rcx
				e:  52                      push   rdx
				f:  56                      push   rsi
				10: 57                      push   rdi
				11: 55                      push   rbp
				12: 41 50                   push   r8
				14: 41 51                   push   r9
				16: 41 52                   push   r10
				18: 41 53                   push   r11
				1a: 41 54                   push   r12
				1c: 41 55                   push   r13
				1e: 41 56                   push   r14
				20: 41 57                   push   r15
				22: 9c                      pushf
				23: 49 c7 c2 e0 43 41 ff    mov    r10, 0xFFFFFFFFFF4143E0
				2a: 41 52                   push   r10
				2c: 48 89 e2                mov    rdx, rsp
				2f: b8 34 00 00 00          mov    eax, 0x34
				34: 0f 05                   syscall
				36: 41 5a                   pop    r10
				38: 9d                      popf
				39: 41 5f                   pop    r15
				3b: 41 5e                   pop    r14
				3d: 41 5d                   pop    r13
				3f: 41 5c                   pop    r12
				41: 41 5b                   pop    r11
				43: 41 5a                   pop    r10
				45: 41 59                   pop    r9
				47: 41 58                   pop    r8
				49: 5d                      pop    rbp
				4a: 5f                      pop    rdi
				4b: 5e                      pop    rsi
				4c: 5a                      pop    rdx
				4d: 59                      pop    rcx
				4e: 5b                      pop    rbx
				4f: 48 b8 00 00 00 00 00    movabs rax, <returnAddress>
				56: 00 00 00
				59: ff e0                   jmp    rax
			*/

			// syscall rva => offset.
			LONG offset = offset0 + 0x20 /* win10 syscall align */ * 0x4;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): allocVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// allocAddress => patch_bytes.
			memcpy(patch_bytes + 0x3, &allocAddress, 8);

			// delay => working_bytes.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[2];
			memcpy(working_bytes + 0x26, &delay_param, 4);

			// returnAddress => working_bytes.
			ULONG64	returnAddress = vmStartAddress + offset + 0xd;
			memcpy(working_bytes + 0x51, &returnAddress, 8);

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes => allocated vm.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}
		}

		if (patchSwitches.NtDelayExecution) { // 0x34

			CHAR patch_bytes[] =
				"\x49\xC7\xC2\xE0\x43\x41\xFF\x4C\x89\x12\x49\x89\xCA\xB8\x34\x00\x00\x00\x0F\x05\xC3";
			/*
				mov r10, 0xFFFFFFFFFF4143E0 ; 1250
				mov qword ptr [rdx], r10
				mov r10, rcx
				mov eax, 0x34
				syscall
				ret
			*/

			// modify delay.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[3];
			memcpy(patch_bytes + 3, &delay_param, 4);

			// syscall rva => offset.
			LONG offset = offset0 + 0x20 * 0x34;

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);
		}

	} else { // if WIN_7

		// win7 ntdll maps 0x10 bytes for each syscall.
		// that's really short to place shellcode, caus 'ret' cannot be moved.
		// fortunately, by assigning param@ZeroBits we can make VirtualAlloc give an addr smaller than 0x1 0000 0000, then
		// 0: use mov eax instead of rax. (rax's high 32-bit is 0 due to x86_64 isa convention)
		// a: [caution] original 'ret' cannot change, for some threads to return correctly from previous syscall.

		CHAR patch_bytes[] = "\xB8\x00\x00\x00\x00\xFF\xE0\x58\x90\x90\xC3";
		/*
			0:  b8 00 00 00 00          mov    eax, <AllocAddress>
			5:  ff e0                   jmp    rax
			7:  58                      pop    rax
			8:  90                      nop
			9:  90                      nop
			a:  c3                      ret        ; previous syscall rip expected returning here.
		*/

		CHAR working_bytes[] =
			"\x49\x89\xCA\xB8\x00\x00\x00\x00\x0F\x05"
			"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
			"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x31\x00\x00\x00\x0F\x05\x41\x5A"
			"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B"
			"\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
		/*
			0:  49 89 ca                mov    r10, rcx
			3:  b8 00 00 00 00          mov    eax, <syscall number>
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
			4e: 48 b8 00 00 00 00 00    movabs rax, <returnAddress>
			55: 00 00 00
			58: ff e0                   jmp    rax
		*/

		if (patchSwitches.NtQueryVirtualMemory) { // 0x20

			// syscall rva => offset.
			LONG offset = offset0 + 0x10 /* win7 syscall align */ * 0x20;

			// allocate vm.
			// allocAddress must < 32 bit, that's ensured by driver's allocator's param@ZeroBits.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): allocVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// allocAddress => patch_bytes.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);

			// syscallNum => working_bytes.
			working_bytes[0x4] = 0x20;

			// delay => working_bytes.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[0];
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// returnAddress => working_bytes.
			ULONG64	returnAddress = vmStartAddress + offset + 0x7;
			memcpy(working_bytes + 0x50, &returnAddress, 8);

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes => allocated vm.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}
		}

		if (patchSwitches.NtWaitForSingleObject) { // 0x1

			// syscall rva => offset.
			LONG offset = offset0 + 0x10 /* win7 syscall align */ * 0x1;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): allocVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// allocAddress => patch_bytes.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);

			// syscallNum => working_bytes.
			working_bytes[0x4] = 0x1;

			// delay => working_bytes.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[2];
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// returnAddress => working_bytes.
			ULONG64	returnAddress = vmStartAddress + offset + 0x7;
			memcpy(working_bytes + 0x50, &returnAddress, 8);

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes => allocated vm.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}
		}

		if (patchSwitches.NtDelayExecution) { // 0x31

			// syscall rva => offset.
			LONG offset = offset0 + 0x10 /* win7 syscall align */ * 0x31;

			// allocate vm.
			PVOID allocAddress = NULL;
			status = driver.allocVM(pid, &allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): allocVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}

			// allocAddress => patch_bytes.
			memcpy(patch_bytes + 0x1, &allocAddress, 4);

			// syscallNum => working_bytes.
			working_bytes[0x4] = 0x31;

			// delay => working_bytes.
			LONG64 delay_param = (LONG64)-10000 * patchDelay[3];
			memcpy(working_bytes + 0x25, &delay_param, 4);

			// returnAddress => working_bytes.
			ULONG64	returnAddress = vmStartAddress + offset + 0x7;
			memcpy(working_bytes + 0x50, &returnAddress, 8);

			// patch_bytes => vmbuf.
			memcpy(vmbuf + offset, patch_bytes, sizeof(patch_bytes) - 1);

			// working_bytes => allocated vm.
			memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
			status = driver.writeVM(pid, vmalloc, allocAddress);
			if (!status) {
				systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
				systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
				return false;
			}
		}
	}


	// write memory.
	status =
	driver.writeVM(pid, vmbuf, (PVOID)vmStartAddress);
	if (!status) {
		systemMgr.log(driver.errorCode, "patch1(): writeVM() failed : %s", driver.errorMessage);
		systemMgr.panic(driver.errorCode, "%s", driver.errorMessage);
		return false;
	}


	// stage1 complete.
	systemMgr.log("patch1(): patch complete.");
	return true;
}

bool PatchManager::_patch_stage2(DWORD pid) {

	win32ThreadManager       threadMgr;
	auto                     osVersion         = systemMgr.getSystemVersion();
	auto                     osBuildNumber     = systemMgr.getSystemBuildNum();
	auto                     vmbuf             = vmbuf_ptr.get();
	auto                     vmalloc           = vmalloc_ptr.get();
	bool                     status;


	// assert: driver loaded.
	systemMgr.log("patch2(): entering.");

	// wait for stable.
	for (auto time = 0; time < 5; time++) {

		if (!patchEnabled || pid != threadMgr.getTargetPid()) {
			systemMgr.log("patch2(): primary wait: pid not match or patch disabled, quit.");
			return false;
		}

		Sleep(1000);
	}


	// find win32u / user32 target offset.
	// target_offset: target syscall entry, from vmbuf:0.
	LONG target_offset = -1;

	// try multi-times to find target offset.
	for (auto try_times = 1; try_times <= 10; try_times++) {
		
		// get potential rip in top 3 threads.
		auto rips = _findRip(true);

		if (rips.empty()) {
			systemMgr.log("patch2(): rips empty, quit.");
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
				systemMgr.log(driver.errorCode, "patch2() warning: load memory failed at 0x%llx : %s", vmStartAddress, driver.errorMessage);
				continue;
			}


			// get mem trait from system version.
			char traits[] = "\x4c\x8b\xd1\xb8\x44\x10\x00\x00";
			if (osVersion == OSVersion::WIN_10_11 && osBuildNumber <= 18363) {
				traits[4] = '\x47';  // win10 1909 and former: 0x1047
			} else if (osVersion == OSVersion::WIN_10_11 && osBuildNumber >= 22000) {
				traits[4] = '\x3f';  // win11: 0x103f
			} // else default to:    // win10 after 1909 || win7: 0x1044


			// loop var to search for syscall entry.
			LONG offset;

			// search round 1: find exact value.
			offset = 0x1000 + *rip % 0x1000 - 0x14;
			for (offset < 0 ? offset = 0 : 0; offset <= 0x4000 - 0x20; offset++) {
				if (0 == memcmp(vmbuf + offset, traits, 8)) { // if strict: 4c 8b d1 b8 XX 10 00 00

					target_offset = offset;

					systemMgr.log("patch2(): strict search: target_offset found at +0x%x (syscall 0x%x)", target_offset, *(LONG*)(traits + 4));
					break;
				}
			}

			// search round 2: if not found, find fuzzy value. (WIN_10_11 only)
			// [remark] WIN_7 do NOT map continuous memory for syscall 0x10xx by a single library (win32u.dll).
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

						systemMgr.log("patch2(): fuzzy search: target_offset found at +0x%x (from syscall 0x%x)", target_offset, found_call_num);
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

						std::unique_ptr<CHAR[]> vmrelate_ptr(new CHAR[0x4000]);
						auto vmrelate = vmrelate_ptr.get();

						// parse instruction: rex.w call.
						auto relative_shift  = *(LONG*)(vmbuf + offset + 0x3);
						auto vaddress_ptr    = vmStartAddress + offset + 0x7 + relative_shift;

						if (!driver.readVM(pid, vmrelate, (PVOID)((vaddress_ptr & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch2(): read *vaddress_ptr (%llx) failed.", vaddress_ptr);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}
						
						// since rex.w call -> vaddress_entry, load memory vaddress_entry points to.
						auto vaddress_entry = *(ULONG64*)(vmrelate + 0x1000 + vaddress_ptr % 0x1000);

						if (!driver.readVM(pid, vmrelate, (PVOID)((vaddress_entry & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch2(): load pages at vaddress_entry = 0x%llx failed.", vaddress_entry);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}

						// check if vaddress_entry points to valid related system call:
						// traits should match and call_number_found should be in range [0x1000, 0x1fff].
						auto call_number_found = *(ULONG*)(vmrelate + 0x1000 + vaddress_entry % 0x1000 + 4);

						if (!(0 == memcmp(vmrelate + 0x1000 + vaddress_entry % 0x1000, traits, 4) &&
							call_number_found >= 0x1000 && call_number_found <= 0x1fff)) {
							fake_entries ++;
							continue;
						}

						// now we have ensured vaddress_entry -> __imp_NtUserXxx... .
						// use call_number_target to shift vaddress_entry to target_entry,
						auto call_number_target = *(ULONG*)(traits + 4);
						auto target_entry       = vaddress_entry - (call_number_found * 0x20) + (call_number_target * 0x20);
						
						// switch to target_entry memory, 
						if (!driver.readVM(pid, vmrelate, (PVOID)((target_entry & ~0xfff) - 0x1000))) {
							systemMgr.log(driver.errorCode, "patch2(): switch win32u pages to vaddress_entry = 0x%llx failed.", vaddress_entry);
							systemMgr.log("  note: %s", driver.errorMessage);
							continue;
						}
						
						// (and check memory to find if it's real target)
						if (!(0 == memcmp(vmrelate + 0x1000 + target_entry % 0x1000, traits, sizeof(traits) - 1))) {
							systemMgr.log(driver.errorCode, "patch2(): it seems that vaddress_entry 0x%llx is inlined, ignored.", vaddress_entry);
							fake_entries++;
							continue;
						}

						// then modify final result.
						target_offset   = 0x1000 + target_entry % 0x1000;
						vmStartAddress  = (target_entry & ~0xfff) - 0x1000;
						memcpy(vmbuf, vmrelate, 0x4000);


						systemMgr.log("patch2(): relative search: target_offset found at +0x%llx, after %d fake traps.", target_offset, fake_entries);
						systemMgr.log("patch2():   >> search path: [rip+0x%x] ~ 0x%llx => 0x%llx (syscall 0x%x) => 0x%llx (syscall 0x%x)",
							relative_shift, vaddress_ptr, vaddress_entry, call_number_found, target_entry, call_number_target);
						
						break;
					}
				}
			}

			if (target_offset == -1) {
				systemMgr.log("patch2(): trait not found at %%rip = %llx.", *rip);
				continue;
			} else {
				systemMgr.log("patch2(): trait found at %%rip = %llx.", *rip);
				break;
			}
		}

		// check if target_offset found in all result in _findRip().
		if (target_offset == -1) {
			systemMgr.log("patch2(): round %u: trait not found in all rips.", try_times);
			Sleep(15000);
			continue;
		} else {
			break;
		}
	}

	// decide whether trait found success in all rounds.
	if (target_offset == -1) {
		systemMgr.log("patch2(): no memory trait found in user32, abort.");
		return false;
	}


	// before manip memory, check process status.
	if (!(patchEnabled && pid == threadMgr.getTargetPid())) {
		systemMgr.log("patch2(): usr switched mode or process terminated, quit.");
		return false;
	}


	// MEMORY PATCH V3

	CHAR working_bytes[] =
		"\x49\x89\xCA\xB8\x44\x10\x00\x00\x0F\x05"
		"\x50\x53\x51\x52\x56\x57\x55\x41\x50\x41\x51\x41\x52\x41\x53\x41\x54\x41\x55\x41\x56\x41\x57\x9C"
		"\x49\xC7\xC2\xE0\x43\x41\xFF\x41\x52\x48\x89\xE2\xB8\x34\x00\x00\x00\x0F\x05\x41\x5A"
		"\x9D\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x41\x5B\x41\x5A\x41\x59\x41\x58\x5D\x5F\x5E\x5A\x59\x5B"
		"\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0";
	/*
		0:  49 89 ca                mov    r10, rcx
		3:  b8 44 10 00 00          mov    eax, <call number>
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
		22: 49 c7 c2 e0 43 41 ff    mov    r10, 0xffffffffff4143e0
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
		4e: 48 b8 00 00 00 00 00    movabs rax, <ReturnAddress>
		55: 00 00 00
		58: ff e0                   jmp    rax
	*/

	// delay => working_bytes.
	LONG64 delay_param = (LONG64)-10000 * patchDelay[1];
	memcpy(working_bytes + 0x25, &delay_param, 4);

	if (osVersion == OSVersion::WIN_10_11) {

		CHAR patch_bytes[] = "\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xE0\x58\xC3\x90\x90\x90\x90\x90\x90\xC3";
		/*
			0:  48 b8 00 00 00 00 00    movabs rax,  <AllocAddress>
			7:  00 00 00
			a:  ff e0                   jmp    rax
			c:  58                      pop    rax
			d:  c3                      ret
			e:  90                      nop
			f:  90                      nop
			10: 90                      nop
			11: 90                      nop
			12: 90                      nop
			13: 90                      nop
			14: c3                      ret         ; previous rip.
		*/

		// allocate vm.
		PVOID allocAddress = NULL;
		status = driver.allocVM(pid, &allocAddress);
		if (!status) {
			systemMgr.log(driver.errorCode, "patch2(): allocVM() failed : %s", driver.errorMessage);
			return false;
		}

		// allocAddress => patch_bytes.
		memcpy(patch_bytes + 0x2, &allocAddress, 8);

		// call num => working_bytes. (WIN_10_11)
		if (osBuildNumber <= 18363) {       // win10 1909 and former: 0x1047
			working_bytes[0x4] = '\x47';
		} else if(osBuildNumber < 22000) {  // win10 after 1909: 0x1044
			working_bytes[0x4] = '\x44';
		} else { // osBuildNumber >= 22000  // win11: 0x103f
			working_bytes[0x4] = '\x3f';
		}

		// returnAddress => working_bytes.
		ULONG64	returnAddress = vmStartAddress + target_offset + 0xc;
		memcpy(working_bytes + 0x50, &returnAddress, 8);

		// patch_bytes => vmbuf.
		memcpy(vmbuf + target_offset, patch_bytes, sizeof(patch_bytes) - 1);

		// working_bytes => allocated vm.
		memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
		status = driver.writeVM(pid, vmalloc, allocAddress);
		if (!status) {
			systemMgr.log(driver.errorCode, "patch2(): writeVM() failed : %s", driver.errorMessage);
			return false;
		}

	} else { // if WIN_7

		CHAR patch_bytes[] = "\xB8\x00\x00\x00\x00\xFF\xE0\x58\x90\x90\xC3";
		/*
			0:  b8 00 00 00 00          mov    eax, <AllocAddress>
			5:  ff e0                   jmp    rax
			7:  58                      pop    rax
			8:  90                      nop
			9:  90                      nop
			a:  c3                      ret        ; previous rip.
		*/

		// if WIN7, use 0x31 to delay.
		working_bytes[0x2f] = '\x31';

		// allocate vm.
		PVOID allocAddress = NULL;
		status = driver.allocVM(pid, &allocAddress);
		if (!status) {
			systemMgr.log(driver.errorCode, "patch2(): allocVM() failed : %s", driver.errorMessage);
			return false;
		}

		// allocAddress => patch_bytes.
		memcpy(patch_bytes + 0x1, &allocAddress, 4);

		// call num => working_bytes. (WIN_7)
		working_bytes[0x4] = '\x44';

		// returnAddress => working_bytes.
		ULONG64	returnAddress = vmStartAddress + target_offset + 0x7;
		memcpy(working_bytes + 0x50, &returnAddress, 8);

		// patch_bytes => vmbuf.
		memcpy(vmbuf + target_offset, patch_bytes, sizeof(patch_bytes) - 1);

		// working_bytes => allocated vm.
		memcpy(vmalloc, working_bytes, sizeof(working_bytes) - 1);
		status = driver.writeVM(pid, vmalloc, allocAddress);
		if (!status) {
			systemMgr.log(driver.errorCode, "patch2(): writeVM() failed : %s", driver.errorMessage);
			return false;
		}
	}

	// write memory.
	status =
	driver.writeVM(pid, vmbuf, (PVOID)vmStartAddress);
	if (!status) {
		systemMgr.log(driver.errorCode, "patch2(): writeVM() failed : %s", driver.errorMessage);
		return false;
	}

	// stage2 complete.
	systemMgr.log("patch2(): patch complete.");
	return true;
}

std::vector<ULONG64>
PatchManager::_findRip(bool useAll) {

	win32ThreadManager                  threadMgr;
	auto&                               threadList   = threadMgr.threadList;
	constexpr auto                      sampleSecs   = 5;
	std::unordered_map<ULONG64, DWORD>  contextMap;  // rip -> visit times
	CONTEXT                             context;
	context.ContextFlags = CONTEXT_ALL;

	char                                logBuf      [1024];
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
				SuspendThread(threadList[i].handle);
				if (GetThreadContext(threadList[i].handle, &context)) {
					contextMap[context.Rip] ++;
				}
				ResumeThread(threadList[i].handle);
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


	strcpy(logBuf, "_findRip(): result: ");
	for (auto item : result) {
		sprintf(logBuf + strlen(logBuf), "%llx ", item);
	}
	systemMgr.log(logBuf);
	
	return result;
}

void PatchManager::_outVmbuf(ULONG64 vmStart, const CHAR* vmbuf) {  // unused: for dbg only

	char title[512];
	time_t t = time(0);
	tm* local = localtime(&t);
	sprintf(title, "[%d-%02d-%02d %02d.%02d.%02d] ",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
	sprintf(title + strlen(title), "vmstart_%llx.txt", vmStart);

	FILE* fp = fopen(title, "w");
	if (fp) {
		for (auto i = 0; i < 0x4000; i++) {
			fprintf(fp, "%02X", (UCHAR)vmbuf[i]);
			if (i % 32 == 31) fprintf(fp, "\n");
		}
		fclose(fp);
	}
	else {
		systemMgr.panic("创建Vmbuf文件失败。");
	}
}