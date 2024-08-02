#include "hal.h"
#include <arch/i686/gdt.h>
#include <arch/i686/idt.h>
#include <arch/i686/isr.h>

bool HAL_Initialize() {
    bool ok = true;
    ok = ok && i686_GDT_Initialize();
    ok = ok && i686_IDT_Initialize();
    ok = ok && i686_ISR_Initialize();
    return ok;
}