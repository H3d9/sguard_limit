PUBLIC DetourMain

EXTERN KeDelayExecutionThread : PROC
EXTERN pOriginal : QWORD


.code

DetourMain PROC PUBLIC

mov    rax,QWORD PTR [rsp]
cmp    DWORD PTR [rax], 088c48148h ; ����������ʱ����������¿��ܷ��ʷǷ�ָ�룻(x86/x64)֧��ֱ�Ӷ�δ4/8�ֽڶ����ָ����м��Ѱַ
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