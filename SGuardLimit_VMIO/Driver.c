// Memory Patch（内核态模块）
// 特别感谢: Zer0Mem0ry BlackBone
#include <ntdef.h>
#include <ntifs.h>
#include <intrin.h>
#include <ntimage.h>

#include "Vad.h"

#define DRIVER_VERSION  "22.11.6"


// 全局对象
RTL_OSVERSIONINFOW   OSVersion;
UNICODE_STRING       dev, dos;
PDEVICE_OBJECT       pDeviceObject;
ULONG                VadRoot;
wchar_t              TargetImageName[256];
PVOID                TargetVad;


// I/O事件和缓冲区结构
#define VMIO_VERSION   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0700, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_READ      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_WRITE     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_ALLOC     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_SUSPEND     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0704, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_RESUME      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0705, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VM_VADSEARCH   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0706, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VM_VADRESTORE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0707, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define PATCH_ACEBASE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0708, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef struct {
	HANDLE   pid;

	PVOID    address;
	CHAR     data[0x1000];

	ULONG    errorCode;
	CHAR     errorFunc[128];
} VMIO_REQUEST;


// windows未公开的结构
typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemModuleInformation = 0x0B,
} SYSTEM_INFORMATION_CLASS;

typedef struct _RTL_PROCESS_MODULE_INFORMATION {
	PVOID Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	CHAR FullPathName[0x0100];
} RTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION Modules[ANYSIZE_ARRAY]; // evil hack
} RTL_PROCESS_MODULES;


// ZwQuerySystemInformation (未声明)
NTSTATUS ZwQuerySystemInformation(
	SYSTEM_INFORMATION_CLASS SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	ULONG* ReturnLength);

// ZwProtectVirtualMemory (win7/win8未导出)
NTSTATUS (NTAPI* ZwProtectVirtualMemory)(
	__in HANDLE ProcessHandle,
	__inout PVOID* BaseAddress,
	__inout PSIZE_T RegionSize,
	__in ULONG NewProtectWin32,
	__out PULONG OldProtect
) = NULL;

// MmCopyVirtualMemory (未声明)
NTSTATUS NTAPI MmCopyVirtualMemory(
	PEPROCESS SourceProcess,
	PVOID SourceAddress,
	PEPROCESS TargetProcess,
	PVOID TargetAddress,
	SIZE_T BufferSize, // @ 1/2/4/8/page only.
	KPROCESSOR_MODE PreviousMode,
	PSIZE_T ReturnSize);

// PsSuspend/ResumeProcess (已导出符号)
NTKERNELAPI NTSTATUS PsSuspendProcess(PEPROCESS Process);
NTKERNELAPI NTSTATUS PsResumeProcess(PEPROCESS Process);


// 包装器
NTSTATUS KeReadVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size) {

	if ((LONG64)SourceAddress < 0) {
		return STATUS_ACCESS_DENIED;
	}

	// 此处MmCopyVirtualMemory仅用于读取目标用户空间的虚拟内存。
	// 尽管MmCopyVirtualMemory也可以拷贝内核空间的分页内存，但若触碰到内核非分页区域或无页表映射区域，
	// 由于对应的内核页表项不存在，触发内核态page fault后缺页中断无法处理，ntos会将之判定为严重错误，若不捕获异常将BSOD。
	// 相应的，若触发了用户态page fault，即使无法解决缺页，也只是MmCopyVirtualMemory返回失败而已。

	SIZE_T Bytes;
	return MmCopyVirtualMemory(Process, SourceAddress, PsGetCurrentProcess(), TargetAddress, Size, KernelMode, &Bytes);
}

NTSTATUS KeWriteVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size) {

	if ((LONG64)TargetAddress < 0) {
		return STATUS_ACCESS_DENIED;
	}
	
	SIZE_T Bytes;
	return MmCopyVirtualMemory(PsGetCurrentProcess(), SourceAddress, Process, TargetAddress, Size, KernelMode, &Bytes);
}


// 内核态模块搜索
typedef struct {
	BOOLEAN          Found;
	ULONG64          VirtualAddress[2];
} SEARCH_RESULT, *PSEARCH_RESULT;

void SearchVad_NT61(PSEARCH_RESULT result, PMMVAD_7 pVad) { // assert: pVad != NULL

	result->Found = FALSE;


	// 若当前Vad节点类型为ImageMap
	if (pVad->u.VadFlags.VadType == VadImageMap) {

		// 若当前节点的FileObject存在
		if (pVad->Subsection && pVad->Subsection->ControlArea && pVad->Subsection->ControlArea->FilePointer.Object) {

			// 取得当前节点对应的映像路径
			PFILE_OBJECT pFileObj = (PFILE_OBJECT)(pVad->Subsection->ControlArea->FilePointer.Value & ~0xf);
			PUNICODE_STRING pImagePath = &pFileObj->FileName;

			// 从路径中获取映像名称
			USHORT imageIndex = -1;
			for (USHORT i = 0; i < pImagePath->Length / 2; i++) {
				if (pImagePath->Buffer[i] == L'\\') {
					imageIndex = i;
				}
			}
			imageIndex++;

			if (imageIndex != 0) {

				// 若映像名称存在，则构造为字符串以供对比
				wchar_t* pImageName = ExAllocatePoolWithTag(NonPagedPool, 512, '9d3H');

				if (pImageName) {

					RtlZeroMemory(pImageName, 512);
					for (USHORT i = 0; i < 255 && imageIndex < pImagePath->Length / 2; i++, imageIndex++) {
						pImageName[i] = pImagePath->Buffer[imageIndex];
					}

					// 对比映像名称是否与待查询的一致
					if (0 == _wcsicmp(pImageName, TargetImageName)) {

						// 若一致，则取该内存镜像的虚拟地址范围为结果
						result->Found = TRUE;
						result->VirtualAddress[0] = pVad->StartingVpn << 12;
						result->VirtualAddress[1] = pVad->EndingVpn << 12;
						
						if (pVad->u.VadFlags.NoChange == 1) {
							pVad->u.VadFlags.NoChange = 0;
							TargetVad = pVad;
						}
					}

					ExFreePoolWithTag(pImageName, '9d3H');
				}
			}
		}
	}

	if (!result->Found && pVad->LeftChild) {
		SearchVad_NT61(result, pVad->LeftChild);
	}

	if (!result->Found && pVad->RightChild) {
		SearchVad_NT61(result, pVad->RightChild);
	}
}

void SearchVad_NT62(PSEARCH_RESULT result, PMMVAD_8 pVad) { // assert: pVad != NULL

	result->Found = FALSE;


	if (pVad->Core.u.VadFlags.VadType == VadImageMap) {

		if (pVad->Subsection && pVad->Subsection->ControlArea && pVad->Subsection->ControlArea->FilePointer.Object) {

			PFILE_OBJECT pFileObj = (PFILE_OBJECT)(pVad->Subsection->ControlArea->FilePointer.Value & ~0xf);
			PUNICODE_STRING pImagePath = &pFileObj->FileName;

			USHORT imageIndex = -1;
			for (USHORT i = 0; i < pImagePath->Length / 2; i++) {
				if (pImagePath->Buffer[i] == L'\\') {
					imageIndex = i;
				}
			}
			imageIndex++;

			if (imageIndex != 0) {

				wchar_t* pImageName = ExAllocatePoolWithTag(NonPagedPool, 512, '9d3H');

				if (pImageName) {

					RtlZeroMemory(pImageName, 512);
					for (USHORT i = 0; i < 255 && imageIndex < pImagePath->Length / 2; i++, imageIndex++) {
						pImageName[i] = pImagePath->Buffer[imageIndex];
					}

					if (0 == _wcsicmp(pImageName, TargetImageName)) {

						result->Found = TRUE;
						result->VirtualAddress[0] = (ULONG64)pVad->Core.StartingVpn << 12;
						result->VirtualAddress[1] = (ULONG64)pVad->Core.EndingVpn << 12;

						if (pVad->Core.u.VadFlags.NoChange == 1) {
							pVad->Core.u.VadFlags.NoChange = 0;
							TargetVad = pVad;
						}
					}

					ExFreePoolWithTag(pImageName, '9d3H');
				}
			}
		}
	}

	if (!result->Found && pVad->Core.VadNode.LeftChild) {
		SearchVad_NT62(result, (PMMVAD_8)pVad->Core.VadNode.LeftChild);
	}

	if (!result->Found && pVad->Core.VadNode.RightChild) {
		SearchVad_NT62(result, (PMMVAD_8)pVad->Core.VadNode.RightChild);
	}
}

void SearchVad_NT10(PSEARCH_RESULT result, PMMVAD_10 pVad) { // assert: pVad != NULL

	result->Found = FALSE;


	// [note] NT10需要依据BuildNumber区分VadType的偏移
	// 如果是NT6.3，同样满足9600 <= 17763。
	ULONG VadType;
	if (OSVersion.dwBuildNumber <= 17763) {
		VadType = pVad->Core.u.VadFlags._17763.VadType;
	} else {
		VadType = pVad->Core.u.VadFlags._18362.VadType;
	}

	if (VadType == VadImageMap) {

		if (pVad->Subsection && pVad->Subsection->ControlArea && pVad->Subsection->ControlArea->FilePointer.Object) {

			PFILE_OBJECT pFileObj = (PFILE_OBJECT)(pVad->Subsection->ControlArea->FilePointer.Value & ~0xf);
			PUNICODE_STRING pImagePath = &pFileObj->FileName;

			USHORT imageIndex = -1;
			for (USHORT i = 0; i < pImagePath->Length / 2; i++) {
				if (pImagePath->Buffer[i] == L'\\') {
					imageIndex = i;
				}
			}
			imageIndex++;

			if (imageIndex != 0) {

				wchar_t* pImageName = ExAllocatePoolWithTag(NonPagedPool, 512, '9d3H');

				if (pImageName) {

					RtlZeroMemory(pImageName, 512);
					for (USHORT i = 0; i < 255 && imageIndex < pImagePath->Length / 2; i++, imageIndex++) {
						pImageName[i] = pImagePath->Buffer[imageIndex];
					}

					if (0 == _wcsicmp(pImageName, TargetImageName)) {

						result->Found = TRUE;
						// [note] NT6.3和后续的NT10使用额外的字节表示第44~47位虚拟地址，且Vpn字段为32位。
						// 而NT6.1的Vpn字段为64位，但仅使用了低32位，它的44~47位虚拟地址从未被使用。
						result->VirtualAddress[0] = ((ULONG64)pVad->Core.StartingVpnHigh << 44) | ((ULONG64)pVad->Core.StartingVpn << 12);
						result->VirtualAddress[1] = ((ULONG64)pVad->Core.EndingVpnHigh << 44) | ((ULONG64)pVad->Core.EndingVpn << 12);
						
						if (OSVersion.dwBuildNumber <= 17763) {
							if (pVad->Core.u.VadFlags._17763.NoChange == 1) {
								pVad->Core.u.VadFlags._17763.NoChange = 0;
								TargetVad = pVad;
							}
						} else {
							if (pVad->Core.u.VadFlags._18362.NoChange == 1) {
								pVad->Core.u.VadFlags._18362.NoChange = 0;
								TargetVad = pVad;
							}
						}
					}

					ExFreePoolWithTag(pImageName, '9d3H');
				}
			}
		}
	}

	if (!result->Found && pVad->Core.VadNode.Left) {
		SearchVad_NT10(result, (PMMVAD_10)pVad->Core.VadNode.Left);
	}

	if (!result->Found && pVad->Core.VadNode.Right) {
		SearchVad_NT10(result, (PMMVAD_10)pVad->Core.VadNode.Right);
	}
}


// 模式匹配
ULONG64 match_pattern(ULONG64 pstart, size_t size, const char* pattern, const char* mask) {

	size_t i, j, patternLen = strlen(mask); // pattern may have '\x00'

	for (i = 0; i < size - patternLen; i++) {
		const char* pnow = (const char*)(pstart + i);

		for (j = 0; j < patternLen; j++) {
			if (!(pnow[j] == pattern[j] || mask[j] == '?')) {
				break;
			}
		}

		if (j == patternLen) {
			return pstart + i;
		}	
	}

	return 0;
}

ULONG64 match_pattern_image(ULONG64 imgbase, const char* secname, const char* pattern, const char* mask) {

	PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)imgbase;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}
	
	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(imgbase + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return 0;
	}

	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
	for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
		
		PIMAGE_SECTION_HEADER p = &section[i];
		if (0 == strcmp((const char*)p->Name, secname)) {
			ULONG64 res = match_pattern(imgbase + p->VirtualAddress, p->Misc.VirtualSize, pattern, mask);
			if (res) {
				return res;
			}
		}
	}

	return 0;
}


// asm跳板
extern void DetourMain();
PVOID pOriginal = DetourMain;
PVOID expectedCip = NULL;


// 各种回调函数
NTSTATUS CreateOrClose(PDEVICE_OBJECT DeviceObject, PIRP irp) {

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}


#define IOCTL_LOG_EXIT(errorName)  if (hProcess) ZwClose(hProcess); \
                                   if (pEProcess) ObDereferenceObject(pEProcess); \
                                   Input->errorCode = RtlNtStatusToDosError(Status); \
                                   memcpy(Input->errorFunc, errorName, sizeof(errorName)); \
                                   Irp->IoStatus.Information = sizeof(VMIO_REQUEST); \
                                   Irp->IoStatus.Status = STATUS_SUCCESS; \
                                   IoCompleteRequest(Irp, IO_NO_INCREMENT); \
                                   return STATUS_SUCCESS;

NTSTATUS IoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {

	ULONG                controlCode  = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.IoControlCode;
	VMIO_REQUEST*        Input        = Irp->AssociatedIrp.SystemBuffer;  /* 用户缓冲区在构造时已清0 */
	PEPROCESS            pEProcess    = NULL;
	SIZE_T               rwSize       = 0x1000;      /* 每次io事件操作的byte数，即sizeof(request->data) */
	SIZE_T               allocSize    = 0x1000 * 4;  /* 若io事件为alloc，则一次性分配4个页面。*/
	HANDLE               hProcess     = NULL;
	OBJECT_ATTRIBUTES    objAttr      = { 0 };
	CLIENT_ID            clientId     = { Input->pid, 0 };
	NTSTATUS             Status       = STATUS_SUCCESS;


	switch (controlCode) {
		
		case VMIO_READ:
		{
			Status = PsLookupProcessByProcessId(Input->pid, &pEProcess);
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_READ::(process not found)");
			}

			Status = KeReadVirtualMemory(pEProcess, Input->address, &Input->data, rwSize);
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_READ::KeReadVirtualMemory");
			}
		}
		break;

		case VMIO_WRITE:
		{
			Status = PsLookupProcessByProcessId(Input->pid, &pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::(process not found)");
			}

			Status = ZwOpenProcess(&hProcess, PROCESS_ALL_ACCESS, &objAttr, &clientId);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::(process not found)");
			}


			ULONG oldProtect;

			// [批注] ZwProtectVirtualMemory即使操作失败也不会BSOD，
			// 因为修改页面保护属性仅需操作页表项而不需要实际读取页面，故并不触发page fault。
			Status = ZwProtectVirtualMemory(hProcess, &Input->address, &rwSize, PAGE_EXECUTE_READWRITE, &oldProtect);
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::ZwProtectVirtualMemory1");
			}

			// 断言：参数 Input->Address 向低地址对齐到页边界（0x1000）。
			Status = KeWriteVirtualMemory(pEProcess, Input->data, Input->address, rwSize);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::KeWriteVirtualMemory");
			}

			Status = ZwProtectVirtualMemory(hProcess, &Input->address, &rwSize, oldProtect, NULL);
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::ZwProtectVirtualMemory2");
			}
		}
		break;

		case VMIO_ALLOC:
		{
			Status = ZwOpenProcess(&hProcess, PROCESS_ALL_ACCESS, &objAttr, &clientId);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_ALLOC::(process not found)");
			}


			PVOID BaseAddress = NULL;

			if (OSVersion.dwMajorVersion == 6 || (OSVersion.dwMajorVersion == 10 && OSVersion.dwBuildNumber == 10240)) {
				// 对于win7/win8/win8.1/win10.10240，约束堆的起始地址在32bit以内，以方便构造shellcode。注：参数ZeroBits的msdn描述有误，详见：
				// https://stackoverflow.com/questions/50429365/what-is-the-most-reliable-portable-way-to-allocate-memory-at-low-addresses-on
				Status = ZwAllocateVirtualMemory(hProcess, &BaseAddress, 0x7FFFFFFF, &allocSize, MEM_COMMIT, PAGE_EXECUTE);
			} else {
				Status = ZwAllocateVirtualMemory(hProcess, &BaseAddress, 0, &allocSize, MEM_COMMIT, PAGE_EXECUTE);
			}
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_ALLOC::ZwAllocateVirtualMemory");
			}

			Input->address = BaseAddress;
		}
		break;

		case IO_SUSPEND:
		{
			Status = PsLookupProcessByProcessId(Input->pid, &pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("IO_SUSPEND::(process not found)");
			}

			Status = PsSuspendProcess(pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("IO_SUSPEND::PsSuspendProcess");
			}
		}
		break;

		case IO_RESUME:
		{
			Status = PsLookupProcessByProcessId(Input->pid, &pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("IO_RESUME::(process not found)");
			}

			Status = PsResumeProcess(pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("IO_RESUME::PsResumeProcess");
			}
		}
		break;

		case VM_VADSEARCH:
		{
			Status = PsLookupProcessByProcessId(Input->pid, &pEProcess);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VM_VADSEARCH::(process not found)");
			}

			Status = ZwOpenProcess(&hProcess, PROCESS_ALL_ACCESS, &objAttr, &clientId);

			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VM_VADSEARCH::(process not found)");
			}


			// 从(wchar_t*)Input->data中取目标模块名称
			RtlZeroMemory(TargetImageName, sizeof(TargetImageName));
			wcscpy_s(TargetImageName, 256, (wchar_t*)Input->data);


			// 搜索目标进程的Vad树以获取在应用层被隐藏的模块位置
			SEARCH_RESULT result;
			RtlZeroMemory(&result, sizeof(result));

			if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1) { // if win7

				PMM_AVL_TABLE_7 pAvlEntry = (PMM_AVL_TABLE_7)((ULONG64)pEProcess + VadRoot);
				PMMVAD_7 root = (PMMVAD_7)(&pAvlEntry->BalancedRoot);

				SearchVad_NT61(&result, root);


			} else if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 2) { // if win8

				PMM_AVL_TABLE_8 pAvlEntry = (PMM_AVL_TABLE_8)((ULONG64)pEProcess + VadRoot);
				PMMVAD_8 root = (PMMVAD_8)(&pAvlEntry->BalancedRoot);

				SearchVad_NT62(&result, root);


			} else { // if win8.1, win10, win11

				PRTL_AVL_TREE pAvlEntry = (PRTL_AVL_TREE)((ULONG64)pEProcess + VadRoot);
				PMMVAD_10 root = (PMMVAD_10)(pAvlEntry->Root);

				if (root) {
					SearchVad_NT10(&result, root);
				}
			}
			

			// 清空缓冲区结构以准备储存搜索结果
			RtlZeroMemory(Input->data, sizeof(Input->data));

			if (result.Found) {

				// 如果找到，则记录目标镜像中所有可执行模块的虚拟地址范围到Input->data，以{0,0}结尾
				// [note] typeof (Input->data) => struct { __int64[2]; } * ;
				ULONG addressCount = 0;
				
				// 搜索目标镜像中可执行模块的位置
				PVOID virtualStart = (PVOID)result.VirtualAddress[0];
				while (virtualStart < (PVOID)result.VirtualAddress[1]) {

					MEMORY_BASIC_INFORMATION memInfo;
					Status = ZwQueryVirtualMemory(hProcess, virtualStart, MemoryBasicInformation, &memInfo, sizeof(memInfo), NULL);
					
					// 如果查询失败，则认为已经结束
					if (!NT_SUCCESS(Status)) {
						break;
					}

					if (memInfo.Protect & PAGE_EXECUTE ||
						memInfo.Protect & PAGE_EXECUTE_READ ||
						memInfo.Protect & PAGE_EXECUTE_READWRITE ||
						memInfo.Protect & PAGE_EXECUTE_WRITECOPY) {

						// 如果是可执行模块，则记录虚拟地址范围以返回给应用层
						((PULONG64)Input->data) [ addressCount++ ] = (ULONG64)memInfo.BaseAddress;
						((PULONG64)Input->data) [ addressCount++ ] = (ULONG64)memInfo.BaseAddress + memInfo.RegionSize;
					}

					virtualStart = (PVOID)((ULONG64)virtualStart + memInfo.RegionSize);
				}
			}
		}
		break;

		case VM_VADRESTORE:
		{
			if (MmIsAddressValid(TargetVad)) {

				if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1) {
					((PMMVAD_7)TargetVad)->u.VadFlags.NoChange = 1;

				} else if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 2) {
					((PMMVAD_8)TargetVad)->Core.u.VadFlags.NoChange = 1;

				} else {
					if (OSVersion.dwBuildNumber <= 17763) {
						((PMMVAD_10)TargetVad)->Core.u.VadFlags._17763.NoChange = 1;
					} else {
						((PMMVAD_10)TargetVad)->Core.u.VadFlags._18362.NoChange = 1;
					}
				}
			}

			TargetVad = NULL;
		}
		break;

		case PATCH_ACEBASE:
		{
			int                    limitMode        = *(int*)Input->data;     // 0: should_exit; 1: obf constant
			RTL_PROCESS_MODULES*   moduleInfo       = NULL;
			ULONG                  infoLength       = 0;
			ULONG64                AceImageBase     = 0;
			ULONG64                AceImageSize     = 0;
			

			// 枚举系统中所有的内核模块，找到ace驱动的地址
			ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &infoLength);

			moduleInfo = ExAllocatePoolWithTag(PagedPool, infoLength, '9d3H');
			
			if (moduleInfo) {

				RtlZeroMemory(moduleInfo, infoLength);
				Status = ZwQuerySystemInformation(SystemModuleInformation, moduleInfo, infoLength, &infoLength);

				if (!NT_SUCCESS(Status)) {
					ExFreePoolWithTag(moduleInfo, '9d3H');
					IOCTL_LOG_EXIT("PATCH_ACEBASE::ZwQuerySystemInformation");
				}

				for (ULONG i = 0; i < moduleInfo->NumberOfModules; i++) {
					if (strstr(moduleInfo->Modules[i].FullPathName, "ACE-BASE")) {
						AceImageBase = (ULONG64)moduleInfo->Modules[i].ImageBase;
						AceImageSize = (ULONG64)moduleInfo->Modules[i].ImageSize;
						break;
					}
				}

				ExFreePoolWithTag(moduleInfo, '9d3H');
			}

			// 若找不到驱动模块，则退出
			if (!AceImageBase) {
				Status = STATUS_NOT_FOUND;
				IOCTL_LOG_EXIT("TP驱动（ACE-BASE.sys）尚未加载到系统内核。您可能没有启动游戏。");
			}


			if (limitMode == 0) {

				// obf_constant:

				ULONG64 targetAddr = match_pattern_image(AceImageBase, ".rdata",
					"\x79\xfa\x73\xfb\x69\xfc\x63\xfd\x61\xfe\x6c\xcd\x6d\xcd\x65", "x?x?x?x?x?x?x?x");

				if (!targetAddr) {
					Status = STATUS_REQUEST_ABORTED;
					if (match_pattern_image(AceImageBase, ".rdata",
						"\x00\xfa\x00\xfb\x00\xfc\x00\xfd\x61\xfe\x6c\xcd\x6d\xcd\x65", "x?x?x?x?x?x?x?x")) {
						IOCTL_LOG_EXIT("您之前已经执行过该操作，不能重复执行。");
					} else {
						IOCTL_LOG_EXIT("未找到特征。因为TP驱动（ACE-BASE.sys）版本不匹配。");
					}
				}


				PMDL pMdl = MmCreateMdl(NULL, (PVOID)(targetAddr & ~0xfff), 0x1000);
				if (!pMdl) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmCreateMdl失败。");
				}

				pMdl->MdlFlags = pMdl->MdlFlags | MDL_MAPPED_TO_SYSTEM_VA;

				MmBuildMdlForNonPagedPool(pMdl);

				PVOID mappedAddr = MmMapLockedPages(pMdl, KernelMode);
				if (NULL == mappedAddr) {
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmMapLockedPages失败。");
				}

				*(ULONG64*)((ULONG64)mappedAddr + (targetAddr & 0xfff)) = 0;

				MmUnmapLockedPages(mappedAddr, pMdl);
				IoFreeMdl(pMdl);

			} else if (limitMode == 1) {

				// cfg hook: 
				//（我在写雨但通篇都不见雨，你能看出我在写谁吗？）

				ULONG64 rip = match_pattern_image(AceImageBase, ".text",
					"\x48\x8B\x54\x24\x60\x48\xC7\xC1\xFF\xFF\xFF\xFF\xFF\x15\x00\x00\x00\x00\x48\x81\xC4\x88\x00\x00\x00\xC3",
					"xxxxxxxxxxxxxx????xxxxxxxx");

				if (!rip) {
					Status = STATUS_REQUEST_ABORTED;
					IOCTL_LOG_EXIT("未找到特征。因为TP驱动（ACE-BASE.sys）版本不匹配。");
				}

				rip = rip + 0x12 + *(LONG*)(rip + 0xe);

				if (!MmIsAddressValid(*(PVOID*)rip)) {
					Status = STATUS_REQUEST_ABORTED;
					IOCTL_LOG_EXIT("MmIsAddressValid断言失败。");
				}

				char shell[] =
					"\x48\x8B\x04\x24\x81\x38\x48\x81\xC4\x88\x75\x20\x52\x51"
					"\x48\xC7\xC0\x80\x69\x67\xFF\x50\x49\x89\xE0\x31\xD2\x31\xC9\x48\xB8\x78\x56\x34\x12\x78\x56\x34\x12\xFF\xD0\x58"
					"\x59\x5A\x48\xB8\x78\x56\x34\x12\x78\x56\x34\x12\xFF\xE0";
				/*
					0:  48 8b 04 24             mov    rax,QWORD PTR [rsp]
					4:  81 38 48 81 c4 88       cmp    DWORD PTR [rax],0x88c48148
					a:  75 20                   jne    2c

					c:  52                      push   rdx
					d:  51                      push   rcx

					e:  48 c7 c0 80 69 67 ff    mov    rax,0xffffffffff676980
					15: 50                      push   rax
					16: 49 89 e0                mov    r8,rsp
					19: 31 d2                   xor    edx,edx
					1b: 31 c9                   xor    ecx,ecx
					1d: 48 b8 78 56 34 12 78    movabs rax,0x1234567812345678
					24: 56 34 12
					27: ff d0                   call   rax
					29: 58                      pop    rax

					2a: 59                      pop    rcx
					2b: 5a                      pop    rdx

					2c: 48 b8 78 56 34 12 78    movabs rax,0x1234567812345678
					33: 56 34 12
					36: ff e0                   jmp    rax
				*/
				
				
				if (0 == memcmp(DetourMain, *(const PVOID*)rip, 0xb)) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("您已经执行过该操作了，不能重复执行。");
				}


				*(PVOID*)(shell + 0x1f) = KeDelayExecutionThread;
				*(PVOID*)(shell + 0x2e) = pOriginal = *(PVOID*)rip;
				
				char* shellSpace = ExAllocatePoolWithTag(PagedPool, 512, '9d3H'); // not intended to free... by mean
				if (!shellSpace) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("ExAllocatePoolWithTag失败。");
				}
				
				RtlCopyMemory(shellSpace, shell, sizeof(shell));


				PMDL pMdl = IoAllocateMdl((PVOID)rip, 8, FALSE, FALSE, NULL);
				if (!pMdl) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("IoAllocateMdl失败。");
				}

				try {
					MmProbeAndLockPages(pMdl, KernelMode, IoReadAccess);
				} except (EXCEPTION_EXECUTE_HANDLER) {
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmProbeAndLockPages失败。");
				}
				
				PLONG64 mapAddress = MmMapLockedPagesSpecifyCache(pMdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
				if (!mapAddress) {
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmMapLockedPagesSpecifyCache失败。");
				}

				Status = MmProtectMdlSystemAddress(pMdl, PAGE_READWRITE);
				if (!NT_SUCCESS(Status)) {
					MmUnmapLockedPages(mapAddress, pMdl);
					MmUnlockPages(pMdl);
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmProtectMdlSystemAddress失败。");
				}

				InterlockedExchange64(mapAddress, (LONG64)DetourMain);

				MmUnmapLockedPages(mapAddress, pMdl);
				MmUnlockPages(pMdl);
				IoFreeMdl(pMdl);

				expectedCip = (PVOID)rip;

			} else {
			
				// data_hijack

				ULONG64 rip = match_pattern_image(AceImageBase, ".text",
					"\x48\xC7\xC2\xFF\xFF\xFF\xFF\x48\x8B\x0D\xFF\xFF\xFF\xFF\xFF\x15", "xxxxxxxxxx????xx");

				if (!rip) {
					Status = STATUS_REQUEST_ABORTED;
					IOCTL_LOG_EXIT("未找到特征1。因为TP驱动（ACE-BASE.sys）版本不匹配。");
				}

				rip = rip + 0xe + *(LONG*)(rip + 0xa);

				if (!MmIsAddressValid((PVOID)rip)) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmIsAddressValid断言失败。");
				}

				PMDL pMdl = IoAllocateMdl((PVOID)rip, 8, FALSE, FALSE, NULL);
				if (!pMdl) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("IoAllocateMdl失败。");
				}

				pMdl->MdlFlags = pMdl->MdlFlags | MDL_MAPPED_TO_SYSTEM_VA;

				MmBuildMdlForNonPagedPool(pMdl);

				PVOID mapAddress = MmMapLockedPages(pMdl, KernelMode);
				if (NULL == mapAddress) {
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmMapLockedPages失败。");
				}

				InterlockedExchange64(mapAddress, 0);

				MmUnmapLockedPages(mapAddress, pMdl);
				IoFreeMdl(pMdl);


				rip = match_pattern_image(AceImageBase, ".text",
					"\x8B\x05\xFF\xFF\xFF\xFF\xF7\x35\xFF\xFF\xFF\xFF", "xx????xx????");

				if (!rip) {
					Status = STATUS_REQUEST_ABORTED;
					IOCTL_LOG_EXIT("未找到特征2。因为TP驱动（ACE-BASE.sys）版本不匹配。");
				}

				rip = rip + 0xc + *(LONG*)(rip + 0x8);

				if (!MmIsAddressValid((PVOID)rip)) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmIsAddressValid断言失败。");
				}

				pMdl = IoAllocateMdl((PVOID)rip, 8, FALSE, FALSE, NULL);
				if (!pMdl) {
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("IoAllocateMdl失败。");
				}

				pMdl->MdlFlags = pMdl->MdlFlags | MDL_MAPPED_TO_SYSTEM_VA;

				MmBuildMdlForNonPagedPool(pMdl);

				mapAddress = MmMapLockedPages(pMdl, KernelMode);
				if (NULL == mapAddress) {
					IoFreeMdl(pMdl);
					Status = STATUS_UNSUCCESSFUL;
					IOCTL_LOG_EXIT("MmMapLockedPages失败。");
				}

				InterlockedExchange64(mapAddress, 2);

				MmUnmapLockedPages(mapAddress, pMdl);
				IoFreeMdl(pMdl);
			}
		}
		break;

		case VMIO_VERSION:
		{
			strcpy_s(Input->data, 256, DRIVER_VERSION);
		}
		break;

		default:
		{
			Status = STATUS_UNSUCCESSFUL;
			IOCTL_LOG_EXIT("H3d9: Bad IO code");
		}
		break;
	}

	if (hProcess) {
		ZwClose(hProcess);
	}

	if (pEProcess) {
		ObDereferenceObject(pEProcess);
	}


	Irp->IoStatus.Information = sizeof(VMIO_REQUEST);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}


NTSTATUS UnloadDriver(PDRIVER_OBJECT pDriverObject) {

	if (expectedCip) {

		PMDL pMdl = IoAllocateMdl((PVOID)expectedCip, 8, FALSE, FALSE, NULL);
		if (!pMdl) {
			return STATUS_UNSUCCESSFUL;
		}

		try {
			MmProbeAndLockPages(pMdl, KernelMode, IoReadAccess);
		} except(EXCEPTION_EXECUTE_HANDLER) {
			IoFreeMdl(pMdl);
			return STATUS_UNSUCCESSFUL;
		}

		PLONG64 mappedAddr = MmMapLockedPagesSpecifyCache(pMdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
		if (!mappedAddr) {
			IoFreeMdl(pMdl);
			return STATUS_UNSUCCESSFUL;
		}

		if (!NT_SUCCESS(MmProtectMdlSystemAddress(pMdl, PAGE_READWRITE))) {
			MmUnmapLockedPages(mappedAddr, pMdl);
			MmUnlockPages(pMdl);
			IoFreeMdl(pMdl);
			return STATUS_UNSUCCESSFUL;
		}

		InterlockedExchange64(mappedAddr, (LONG64)pOriginal);

		MmUnmapLockedPages(mappedAddr, pMdl);
		MmUnlockPages(pMdl);
		IoFreeMdl(pMdl);
	}

	IoDeleteSymbolicLink(&dos);
	IoDeleteDevice(pDriverObject->DeviceObject);

	return STATUS_SUCCESS;
}


// 入口点
NTSTATUS DriverEntry(
	PDRIVER_OBJECT pDriverObject, 
	PUNICODE_STRING pRegistryPath) {

	// 设置回调函数组
	pDriverObject->MajorFunction[IRP_MJ_CREATE]         = CreateOrClose;    // <- CreateFile()
	pDriverObject->MajorFunction[IRP_MJ_CLOSE]          = CreateOrClose;    // <- CloseHandle()
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoControl;        // <- DeviceIoControl()
	pDriverObject->DriverUnload                         = UnloadDriver;     // <- service stop


	// 获取操作系统版本
	memset(&OSVersion, 0, sizeof(RTL_OSVERSIONINFOW));
	OSVersion.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
	RtlGetVersion(&OSVersion);


	// 获取VadRoot在_EPROCESS中的偏移
	if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1) {  // Win 7 (SP0, SP1)
		VadRoot = 0x448;

	} else if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 2) {  // Win 8
		VadRoot = 0x590;

	} else if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 3) {  // Win 8.1
		VadRoot = 0x5D8;

	} else if (OSVersion.dwMajorVersion == 10 && OSVersion.dwMinorVersion == 0) {

		if (OSVersion.dwBuildNumber < 22000) {  // Win 10
			switch (OSVersion.dwBuildNumber) {
			case 10240:
				VadRoot = 0x608;
				break;
			case 10586:
				VadRoot = 0x610;
				break;
			case 14393:
				VadRoot = 0x620;
				break;
			case 15063: case 16299: case 17134: case 17763:
				VadRoot = 0x628;
				break;
			case 18362: case 18363:
				VadRoot = 0x658;
				break;
			case 19041: case 19042: case 19043: case 19044:
				VadRoot = 0x7D8;
				break;
			}
			if (OSVersion.dwBuildNumber > 19044) { // Win 10 latest (22.6.24 beta, 19045)
				VadRoot = 0x7D8;
			}

		} else { // if (OSVersion.dwBuildNumber <= 22621)  Win 11 latest (22.6.28 beta branch)
			VadRoot = 0x7D8;
		}
	}

	if (!VadRoot) {
		return STATUS_NOT_SUPPORTED;
	}


	// 手动获取ZwProtectVirtualMemory的地址，该函数仅在win8.1（NT6.3）和之后的系统版本导出。
	UNICODE_STRING ZwProtectVirtualMemoryName;
	RtlInitUnicodeString(&ZwProtectVirtualMemoryName, L"ZwProtectVirtualMemory");
	ZwProtectVirtualMemory = MmGetSystemRoutineAddress(&ZwProtectVirtualMemoryName);

	if (!ZwProtectVirtualMemory) {

		// 如果未导出目标函数，且不是win7（NT6.1）或win8（NT6.2），则退出。
		if (!(OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1) &&
			!(OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 2)) {
			return STATUS_NOT_IMPLEMENTED;
		}

		// 尝试手动获取ZwProtectVirtualMemory的虚拟地址。
		// 幸运的是，尽管nt内核没有导出所有Zw函数，但所有SSDT中标明的服务都在内核中存在镜像入口，
		// 此外所有的Zw函数都按0x20字节对齐，且依据SSDT中的顺序映射到连续的虚拟内存空间。
		// 只要得到一个必定导出的Zw函数和目标Zw函数的系统服务号，就可以得到目标Zw函数的入口地址。

		// 获取系统服务 ZwClose 的入口地址，该函数必定导出。
		UNICODE_STRING ZwCloseName;
		RtlInitUnicodeString(&ZwCloseName, L"ZwClose");
		ULONG64 ZwClose = (ULONG64)MmGetSystemRoutineAddress(&ZwCloseName);

		// 依据ZwClose的地址，求解ZwProtectVirtualMemory的地址。
		if (OSVersion.dwMinorVersion == 1) {
			ZwProtectVirtualMemory = (PVOID)(ZwClose - 0x20 * 0xC + 0x20 * 0x4D); // NT 6.1
		} else {
			ZwProtectVirtualMemory = (PVOID)(ZwClose - 0x20 * 0xD + 0x20 * 0x4E); // NT 6.2
		}
	}


	// 初始化符号链接和I/O设备
	RtlInitUnicodeString(&dev, L"\\Device\\sguard_limit");
	RtlInitUnicodeString(&dos, L"\\DosDevices\\sguard_limit");
	IoCreateSymbolicLink(&dos, &dev);

	IoCreateDevice(pDriverObject, 0, &dev, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &pDeviceObject);
	if (pDeviceObject) {
		pDeviceObject->Flags |= DO_DIRECT_IO;
		pDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	} else {
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}