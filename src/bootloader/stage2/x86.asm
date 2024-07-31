global x86_OutB
x86_OutB:
    [bits 32]
    mov dx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

global x86_InB
x86_InB:
    [bits 32]
    mov dx, [esp + 4]
    xor eax, eax
    in al, dx
    ret
    