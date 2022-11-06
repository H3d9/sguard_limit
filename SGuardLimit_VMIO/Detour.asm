PUBLIC DetourMain

EXTERN KeDelayExecutionThread : PROC
EXTERN pOriginal : QWORD


.code

DetourMain PROC PUBLIC

mov    rax,QWORD PTR [rsp]
cmp    DWORD PTR [rax], 088c48148h
jne    L1

push   rdx
push   rcx

mov    rax, 0ffffffffff676980h
push   rax
mov    r8, rsp
xor    edx, edx
xor    ecx, ecx
mov    rax, KeDelayExecutionThread

call   rax
pop    rax

pop    rcx
pop    rdx

L1:
mov    rax, pOriginal
jmp    rax

DetourMain ENDP

end