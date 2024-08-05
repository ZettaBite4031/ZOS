bits 32

; void __attribute__((cdecl)) i686_IDT_Load(IDT_Descriptor* descriptor);
global i686_IDT_Load
i686_IDT_Load:
    push ebp
    mov ebp, esp

    mov eax, [bp + 8]
    lidt [eax]

    mov esp, ebp
    pop ebp
    ret
