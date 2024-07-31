#pragma once
#include <stdint.h>
#include <stdbool.h>


void cls();
void setcursor(int x, int y);
void putc(char c);
void putcolor(int x, int y, uint8_t color);
void puts(const char* str);
void printf(const char* fmt, ...);
void print_buffer(const char* msg, const void* buffer, uint32_t count);
