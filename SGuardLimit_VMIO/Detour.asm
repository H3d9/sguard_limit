PUBLIC DetourMain

EXTERN KeDelayExecutionThread : PROC
EXTERN pOriginal : QWORD


.code

DetourMain PROC PUBLIC

mov    rax,QWORD PTR [rsp]
cmp    DWORD PTR [rax], 88c48148h ; ����������ʱ����������¿��ܷ��ʷǷ�ָ�룻(x86/x64)֧��ֱ�Ӷ�δ4/8�ֽڶ����ָ����м��Ѱַ
jne    L1

mov    qword ptr[rsp+8], rcx
mov    qword ptr[rsp+10h], rdx

sub    rsp, 38h
mov    qword ptr[rsp+20h], 0ffffffffff676980h
lea    r8, qword ptr[rsp+20h]
xor    edx, edx
xor    ecx, ecx
call   KeDelayExecutionThread
add    rsp, 38h

mov    rcx, qword ptr[rsp+8]
mov    rdx, qword ptr[rsp+10h]

L1:
jmp    pOriginal

DetourMain ENDP

end