bits 32

%macro ISR_NOERRORCODE 1
global i686_ISR%1
i686_ISR%1:
    push 1
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERRORCODE 1
global i686_ISR%1
i686_ISR%1:
    push %1
    jmp isr_common
%endmacro

%include "arch/i686/isr_gen.inc"

extern i686_ISR_Handler
isr_common:
    pusha

    xor eax, eax
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call i686_ISR_Handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret
