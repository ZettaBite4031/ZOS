#pragma once
#include <stdint.h>


void __attribute__((cdecl)) x86_OutB(uint16_t port, uint8_t value);
uint8_t __attribute__((cdecl)) x86_InB(uint16_t port);
