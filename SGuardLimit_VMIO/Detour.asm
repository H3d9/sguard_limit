PUBLIC DetourMain

EXTERN KeDelayExecutionThread : PROC
EXTERN pOriginal : QWORD


.code

DetourMain PROC PUBLIC

mov    rax,QWORD PTR [rsp]
cmp    DWORD PTR [rax], 088c48148h ; 查找特征码时，极端情况下可能访问非法指针；(x86/x64)支持直接对未4/8字节对齐的指针进行间接寻址
jne    L1

push   r8
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
pop    r8

L1:
mov    rax, pOriginal
jmp    rax

DetourMain ENDP

end