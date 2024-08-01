#include "idt.h"
#include <stdint.h>
#include <util/binary.h>

typedef struct {
    uint16_t BaseLow;
    uint16_t SegmentSelector;
    uint8_t Reserved;
    uint8_t Flags;
    uint8_t BaseHigh;
} __attribute__((packed)) IDT_Entry;

typedef struct {
    uint16_t Limit;
    IDT_Entry* Entry;
} __attribute__((packed)) IDT_Descriptor;

IDT_Entry g_IDT[256];

IDT_Descriptor g_IDTDescriptor = { sizeof(g_IDT) - 1, g_IDT };

bool __attribute__((cdecl)) i686_IDT_Load(IDT_Descriptor* descriptor);

void i686_IDT_SetGate(int interrupt, void* base, uint16_t segmentDescriptor, uint8_t flags) {
    g_IDT[interrupt].BaseLow = ((uint32_t)base) & 0xFFFF;
    g_IDT[interrupt].SegmentSelector = segmentDescriptor;
    g_IDT[interrupt].Reserved = 0x0;
    g_IDT[interrupt].Flags = flags;
    g_IDT[interrupt].BaseHigh = ((uint32_t)base >> 16) & 0xFFFF;
}

void i686_IDT_EnableGate(int interrupt) {
    FLAG_SET(g_IDT[interrupt].Flags, IDT_FLAG_PRESENT);
}

void i686_IDT_DisableGate(int interrupt) {
    FLAG_UNSET(g_IDT[interrupt].Flags, IDT_FLAG_PRESENT);
}

bool i686_IDT_Initialize() {
    return i686_IDT_Load(&g_IDTDescriptor);
}