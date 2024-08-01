#include <stdint.h>
#include "stdio.h"
#include "disk.h"

void* g_Data = (void*)0x20000;

void __attribute__((cdecl)) start(uint16_t driveNumber) {
    cls();
    printf("=-=-=-=-= ZOS STAGE2 LOADING =-=-=-=-=\n\n");

    DISK disk;
    if (!DISK_Initialize(&disk, driveNumber)) {
        printf("DISK: Failed to initialize the disk!\n");
        goto end;
    }
    
    printf("DISK: Initialized disk #%d\n", driveNumber);
    printf("DISK: Drive ID - %d\nDISK: Drive Heads - %d\nDISK: Drive Cylinders - %d\nDISK: Drive Sectors - %d\n", 
            disk.id, disk.heads, disk.cylinders, disk.sectors);

    if (!DISK_ReadSectors(&disk, 0x0, 1, g_Data)) {
        printf("DISK: Failed read sectors test!\n");
        goto end;
    }
    printf("Sucessfully read from disk.\n\n");
    
    print_buffer("Boot Sector: ", g_Data, 512);

end:
    for(;;);
}