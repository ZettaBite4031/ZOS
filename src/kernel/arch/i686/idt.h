#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IDT_FLAG_GATE_TASK          = 0x5,
    IDT_FLAG_GATE_16BIT_INT     = 0x6,
    IDT_FLAG_GATE_16BUT_TRAP    = 0x7,
    IDT_FLAG_GATE_32BIT_INT     = 0xE,
    IDT_FLAG_GATE_32BIT_TRAP    = 0x7,

    IDT_FLAG_RING0              = (0 << 5),
    IDT_FLAG_RING1              = (1 << 5),
    IDT_FLAG_RING2              = (2 << 5),
    IDT_FLAG_RING3              = (3 << 5),

    IDT_FLAG_PRESENT            = 0x80,
} IDT_FLAGS;

bool i686_IDT_Initialize();

void i686_IDT_EnableGate(int interrupt);
void i686_IDT_DisableGate(int interrupt);
void i686_IDT_SetGate(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags);