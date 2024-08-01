#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <hal/hal.h>

extern uint8_t __bss_start;
extern uint8_t __end;

void __attribute__((section(".entry"))) start() {
    memset(&__bss_start, 0, &__end - &__bss_start);

    cls();
    printf("=-=-=-=-= ZOS KERNEL LOADING =-=-=-=-=\n\n");

    if (!HAL_Initialize()) {
        printf("HAL: Failed to initialize!\n");
        goto end;
    }
    printf("HAL: Initialized.\n");
    
    printf("\nWelcome to the ZettabiteOS Kernel.\n");
end:
    for(;;);
}