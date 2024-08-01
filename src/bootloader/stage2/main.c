#include <stdint.h>
#include "stdio.h"
#include "disk.h"
#include "fat.h"
#include "memory.h"
#include "memdefs.h"

uint8_t* KernelLoadBuffer = (uint8_t*)LOAD_KERNEL_ADDR;
uint8_t* Kernel = (uint8_t*)MEMORY_KERNEL_ADDR;

typedef void(*KernelStart)();

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
    
    putc('\n');
    if (!FAT_Initialize(&disk)) {
        printf("FAT: Failed to initialize FAT for Disk%d\n", disk.id);
        goto end;
    }
    printf("FAT: Successfully initialized FAT for Disk%d\n\n", disk.id);


    // find kernel
    FAT_File* fd = FAT_Open(&disk, "/kernel.bin");
    uint32_t read;
    uint8_t* kernelBuffer = Kernel;
    while ((read = FAT_Read(&disk, fd, MEMORY_FAT_SIZE, KernelLoadBuffer))) {
        memcpy(kernelBuffer, KernelLoadBuffer, read);
        kernelBuffer += read;
    }
    FAT_Close(fd);

    // load kernel
    KernelStart kernelStart = (KernelStart)Kernel;
    kernelStart();

end:
    printf("Failed to load the kernel!\n");
    for(;;);
}