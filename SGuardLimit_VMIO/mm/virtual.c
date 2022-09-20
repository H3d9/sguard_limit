#include <ntifs.h>

NTSTATUS
NTAPI
MiProtectVirtualMemory(IN PEPROCESS Process,
                       IN OUT PVOID* BaseAddress,
                       IN OUT PSIZE_T NumberOfBytesToProtect,
                       IN ULONG NewAccessProtection,
                       OUT PULONG OldAccessProtection OPTIONAL) {
    
    return STATUS_SUCCESS;
}