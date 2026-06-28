/* flash.c */
#include "flash.h"
#include "stm32f4xx.h"

void Flash_Unlock(void)
{
    if (FLASH->CR & (1 << 31))          /* LOCK bit set */
    {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

void Flash_Lock(void)
{
    FLASH->CR |= (1 << 31);
}

static void flash_wait(void)
{
    while (FLASH->SR & (1 << 16));      /* BSY */
}

void Flash_EraseSector(uint8_t sector)
{
    flash_wait();
    FLASH->CR &= ~(0xF << 3);          /* clear SNB */
    FLASH->CR |=  (sector << 3);        /* set sector number */
    FLASH->CR |=  (1 << 1);             /* SER = sector erase */
    FLASH->CR |=  (1 << 16);            /* STRT */
    flash_wait();
    FLASH->CR &= ~(1 << 1);             /* clear SER */
}

void Flash_WriteBuffer(uint32_t addr, uint8_t* data, uint32_t len)
{
    flash_wait();
    FLASH->CR &= ~(0x3 << 8);          /* PSIZE = 00 = byte writes */
    FLASH->CR |=  (1 << 0);             /* PG = program enable */

    for (uint32_t i = 0; i < len; i++)
    {
        *(volatile uint8_t*)(addr + i) = data[i];
        flash_wait();
    }

    FLASH->CR &= ~(1 << 0);             /* clear PG */
}
