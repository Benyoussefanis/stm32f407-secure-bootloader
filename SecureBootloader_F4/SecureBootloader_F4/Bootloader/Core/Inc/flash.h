#ifndef FLASH_H
#define FLASH_H
#include <stdint.h>

void Flash_Unlock(void);
void Flash_Lock(void);
void Flash_EraseSector(uint8_t sector);
void Flash_WriteBuffer(uint32_t addr, uint8_t* data, uint32_t len);

#endif
