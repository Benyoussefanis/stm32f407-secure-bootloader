#ifndef BOOTLOADER_H
#define BOOTLOADER_H
#include <stdint.h>

#define UPDATE_MAGIC         0xDEADC0DEUL
#define APP_ADDRESS          0x08008000UL
#define APP_SECTOR_FIRST     2
#define APP_SECTOR_LAST      5

#define DOWNLOAD_SLOT_ADDR   0x08040000UL
#define DOWNLOAD_SLOT_SECTOR 6
#define DOWNLOAD_SLOT_SIZE   (128 * 1024)

#define VERSION_STORE_ADDR   0x08060000UL
#define VERSION_STORE_SECTOR 7
#define VERSION_MAGIC        0xBEEF1234UL  // written next to version so we know it's valid

void Bootloader_Run(void);

#endif
