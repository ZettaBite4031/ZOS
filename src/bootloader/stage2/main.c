#include <stdint.h>
#include "stdio.h"
#include "disk.h"
#include "fat.h"
#include "memory.h"

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

    FAT_File* fd = FAT_Open(&disk, "/");
    FAT_DirectoryEntry entry;
    int i = 0; 
    printf("=-= ZOS ROOT DIRECTORY =-=\n");
    while (FAT_ReadEntry(&disk, fd, &entry) && i++ < 5) {
        printf(" ");
        for (int i = 0; i < 11; i++)
            putc(entry.Name[i]);
        printf("\n");
    }
    FAT_Close(fd);

    // read test file
    char buffer[256];
    uint32_t read;
    printf("\n");
    fd = FAT_Open(&disk, "mydir/test.txt");
    while ((read = FAT_Read(&disk, fd, sizeof(buffer), buffer))) {
        for (uint32_t i = 0; i < read; i++) {
            putc(buffer[i]);
        }
    }
    FAT_Close(fd);

end:
    for(;;);
}