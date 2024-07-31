bits 16

section .entry

extern __bss_start
extern __end

extern start
global entry

entry:
    cli

    ; save boot drive
    mov [g_BootDrive], dl

    ; setup stack
    mov ax, ds
    mov ss, ax
    mov sp, 0xFFF0
    mov bp, sp
    
    ; begin switch to protected mode
    ; 1 - disable interrupts. Already done.
    ; 2 - Enable the A20 gate
    call EnableA20
    ; 3 - Load the Global Descriptor Table (GDT)
    call LoadGDT

    ; 4 - set protection enable flag in CR0
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; 5 - perform a far jump into protected mode
    jmp dword 08h:.ProtectedMode

    .ProtectedMode:
    [bits 32]

    ; 6 - setup segment registers
    mov eax, 0x10
    mov ds, ax
    mov ss, ax

    ; Protected Mode entered

    ; clear bss (unitialized data)
    mov edi, __bss_start
    mov ecx, __end
    sub ecx, edi
    mov al, 0
    ; cld clears the direction flag to make sure rep moves forwards instead of backwards
    ; rep repeats a command while ecx is greater than zero 
    ; stosb (store single byte) takes the value in al and stores it in the memory pointed to by edi. 
    cld
    rep stosb


    ; expect boot drive in dl, send it as argument to cstart function
    xor edx, edx
    mov dl, [g_BootDrive]
    push edx
    call start


HALT:
    cli
    hlt
    jmp HALT

EnableA20:
    [bits 16]

    ; disable the keyboard
    call A20WaitInput
    mov al, KbdCtrlDisableKeyboard
    out KbdCtrlCommandPort, al

    ; read control output port
    call A20WaitInput
    mov al, KbdCtrlReadCtrlOutputPort
    out KbdCtrlCommandPort, al

    call A20WaitOutput
    in al, KbdCtrlDataPort
    push eax

    ; write control output port
    call A20WaitInput
    mov al, KbdCtrlWriteCtrlOutputPort
    out KbdCtrlCommandPort, al

    call A20WaitInput
    pop eax
    or al, 2
    out KbdCtrlDataPort, al

    ; enable keyboard
    call A20WaitInput
    mov al, KbdCtrlEnableKeyboard
    out KbdCtrlCommandPort, al

    call A20WaitInput
    ret

A20WaitInput:
    [bits 16]
    ; wait until status bit 2 (input buffer) is 0
    ; by reading command port, the status byte is read
    in al, KbdCtrlCommandPort
    test al, 2
    jnz A20WaitInput
    ret

A20WaitOutput:
    [bits 16]
    ; wait until status bit 1 (output buffer) is 1
    ; meaning data is ready
    in al, KbdCtrlCommandPort
    test al, 1
    jz A20WaitOutput
    ret

LoadGDT:
    [bits 16]
    lgdt [g_GDTDesc]
    ret

KbdCtrlDataPort             equ 0x60
KbdCtrlCommandPort          equ 0x64
KbdCtrlDisableKeyboard      equ 0xAD
KbdCtrlEnableKeyboard       equ 0xAE
KbdCtrlReadCtrlOutputPort   equ 0xD0
KbdCtrlWriteCtrlOutputPort  equ 0xD1

g_GDT:      ; NULL descriptor
            dq 0

            ; 32-bit code segment
            dw 0FFFFh                   ; limit (bits 0-15) = 0xFFFFF for full 32-bit range
            dw 0                        ; base (bits 0-15) = 0x0
            db 0                        ; base (bits 16-23)
            db 10011010b                ; access (present, ring 0, code segment, executable, direction 0, readable)
            db 11001111b                ; granularity (4k pages, 32-bit pmode) + limit (bits 16-19)
            db 0                        ; base high

            ; 32-bit data segment
            dw 0FFFFh                   ; limit (bits 0-15) = 0xFFFFF for full 32-bit range
            dw 0                        ; base (bits 0-15) = 0x0
            db 0                        ; base (bits 16-23)
            db 10010010b                ; access (present, ring 0, data segment, executable, direction 0, writable)
            db 11001111b                ; granularity (4k pages, 32-bit pmode) + limit (bits 16-19)
            db 0                        ; base high

            ; 16-bit code segment
            dw 0FFFFh                   ; limit (bits 0-15) = 0xFFFFF
            dw 0                        ; base (bits 0-15) = 0x0
            db 0                        ; base (bits 16-23)
            db 10011010b                ; access (present, ring 0, code segment, executable, direction 0, readable)
            db 00001111b                ; granularity (1b pages, 16-bit pmode) + limit (bits 16-19)
            db 0                        ; base high

            ; 16-bit data segment
            dw 0FFFFh                   ; limit (bits 0-15) = 0xFFFFF
            dw 0                        ; base (bits 0-15) = 0x0
            db 0                        ; base (bits 16-23)
            db 10010010b                ; access (present, ring 0, data segment, executable, direction 0, writable)
            db 00001111b                ; granularity (1b pages, 16-bit pmode) + limit (bits 16-19)
            db 0                        ; base high

g_GDTDesc:  dw g_GDTDesc - g_GDT - 1    ; limit = size of GDT
            dd g_GDT                    ; address of GDT

g_BootDrive: db 0