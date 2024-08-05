bits 32

; void __attribute__((cdecl)) i686_GDT_Load(GDT_Descriptor* descriptor, uint16_t codeSegment, uint16_t dataSegment);
global i686_GDT_Load
i686_GDT_Load:
    push ebp 
    mov ebp, esp

    ; load gdt
    mov eax, [ebp + 8] ; GDT Descriptor
    lgdt [eax]

    ; reload code segment
    mov eax, [ebp + 12] ; code segment 
    push eax
    push .reload_cs
    retf 
    .reload_cs:

    ; reload data segment
    mov ax, [bp + 16] ; data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, ebp
    pop ebp
    ret