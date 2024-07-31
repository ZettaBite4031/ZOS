#include <stdint.h>
#include "stdio.h"

void __attribute__((cdecl)) start(uint16_t driveNumber) {
    (driveNumber);
    cls();
    
    puts("String!?\n");
    printf("This is a number: %d\n", 1234);

    for(;;);
}