#pragma once
#include <stdbool.h>

#define i686_GDT_CODE_SEGMENT 0x08
#define i686_GDT_DATA_SEGMENT 0x10

bool i686_GDT_Initialize();