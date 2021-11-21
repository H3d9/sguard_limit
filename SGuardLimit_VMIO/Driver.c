// Memory Patch（内核态模块）
// 特别感谢: Zer0Mem0ry 提供的声明
#include <ntdef.h>
#include <ntifs.h>


// 全局对象
RTL_OSVERSIONINFOW   OSVersion;
UNICODE_STRING       dev, dos;
PDEVICE_OBJECT       pDeviceObject;


// I/O接口事件和缓冲区结构
#define VMIO_READ   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0701, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_WRITE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0702, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VMIO_ALLOC  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0703, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_SUSPEND  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0704, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_RESUME   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0705, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef struct {
	HANDLE   pid;

	PVOID    address;
	CHAR     data[0x1000];

	ULONG    errorCode;
	CHAR     errorFunc[128];
} VMIO_REQUEST;


// ZwProtectVirtualMemory（在win7中未导出，但在win10中导出。为保证系统兼容性，需要动态获取地址）
NTSTATUS(NTAPI* ZwProtectVirtualMemory)(
	__in HANDLE ProcessHandle,
	__inout PVOID* BaseAddress,
	__inout PSIZE_T RegionSize,
	__in ULONG NewProtectWin32,
	__out PULONG OldProtect
) = NULL;

// MmCopyVirtualMemory (在ntoskrnl.lib中存在，但未被微软公开)
NTSTATUS NTAPI MmCopyVirtualMemory(
	PEPROCESS SourceProcess,
	PVOID SourceAddress,
	PEPROCESS TargetProcess,
	PVOID TargetAddress,
	SIZE_T BufferSize, // @ 1/2/4/8/page only.
	KPROCESSOR_MODE PreviousMode,
	PSIZE_T ReturnSize);

// PsSuspend/ResumeProcess (已导出，但未被公开)
NTKERNELAPI
NTSTATUS
PsSuspendProcess(PEPROCESS Process);

NTKERNELAPI
NTSTATUS
PsResumeProcess(PEPROCESS Process);


// 包装器
NTSTATUS KeReadVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size) {
	SIZE_T Bytes;
	return MmCopyVirtualMemory(Process, SourceAddress, PsGetCurrentProcess(), TargetAddress,
		Size, KernelMode, &Bytes);
}

NTSTATUS KeWriteVirtualMemory(PEPROCESS Process, PVOID SourceAddress, PVOID TargetAddress, SIZE_T Size) {
	SIZE_T Bytes;
	return MmCopyVirtualMemory(PsGetCurrentProcess(), SourceAddress, Process, TargetAddress,
		Size, KernelMode, &Bytes);
}


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
	VMIO_REQUEST*        Input        = Irp->AssociatedIrp.SystemBuffer;  /* copied from user space */
	PEPROCESS            pEProcess    = NULL;
	SIZE_T               rwSize       = 0x1000;      /* bytes io in an ioctl. = sizeof(request->data) */
	SIZE_T               allocSize    = 0x1000 * 4;  /* alloc 4 pages in an ioctl. */
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

			Status = ZwProtectVirtualMemory(hProcess, &Input->address, &rwSize, PAGE_EXECUTE_READWRITE, &oldProtect);
			
			if (!NT_SUCCESS(Status)) {
				IOCTL_LOG_EXIT("VMIO_WRITE::ZwProtectVirtualMemory1");
			}

			// assert:  Input->Address is round down to 1 page.
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

			if (OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1) {
				// 对于win7，约束堆的起始地址在32bit以内，以方便构造shellcode。注：参数ZeroBits的msdn描述有误，详见：
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

	IoDeleteSymbolicLink(&dos);
	IoDeleteDevice(pDriverObject->DeviceObject);

	return STATUS_SUCCESS;
}


// 驱动程序的入口点
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


	// win7x64的ntoskrnl.exe并没有导出ZwProtectVirtualMemory，无法直接引用。
	// 若直接（隐式）动态链接，则win7的PEloader将找不到该函数入口导致驱动加载失败，故此处手动获取地址。

	UNICODE_STRING ZwProtectVirtualMemoryName;
	RtlInitUnicodeString(&ZwProtectVirtualMemoryName, L"ZwProtectVirtualMemory");
	ZwProtectVirtualMemory = MmGetSystemRoutineAddress(&ZwProtectVirtualMemoryName);

	if (!ZwProtectVirtualMemory) {

		// 如果未导出目标函数，且不是win7（NT6.1），则退出。
		if (!(OSVersion.dwMajorVersion == 6 && OSVersion.dwMinorVersion == 1)) {
			return STATUS_UNSUCCESSFUL;
		}

		// 若是win7，则尝试手动获取ZwProtectVirtualMemory的虚拟地址。
		// 幸运的是，尽管win7的nt内核没有导出所有Zw函数，但所有SSDT中标明的服务都在内核中存在镜像入口，
		// 此外所有的Zw函数都按0x20字节对齐，且依据SSDT中的顺序映射到连续的虚拟内存空间。
		// 只要得到一个必定导出的Zw函数和目标Zw函数的系统服务号，就可以得到目标Zw函数的入口地址。

		// 获取ZwClose的入口地址，该函数必定导出。
		UNICODE_STRING ZwCloseName;
		RtlInitUnicodeString(&ZwCloseName, L"ZwClose");
		ULONG64 ZwClose = (ULONG64)MmGetSystemRoutineAddress(&ZwCloseName);

		// 获取ZwClose的系统服务号。
		// 由于我们知道NT6.1的ZwClose服务号，故这一步可省略。
		ULONG ZwCloseId = *(ULONG*)(ZwClose + 0x15);

		// 索引到0号系统服务的地址，再索引到0x4D号系统服务的地址。
		ZwProtectVirtualMemory = (PVOID)(ZwClose - 0x20 * ZwCloseId + 0x20 * 0x4D);
	}


	// 初始化符号链接和I/O设备
	RtlInitUnicodeString(&dev, L"\\Device\\SGuardLimit_VMIO");
	RtlInitUnicodeString(&dos, L"\\DosDevices\\SGuardLimit_VMIO");
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