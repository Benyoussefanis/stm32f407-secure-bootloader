#include "crc.h"
#include "stm32f4xx.h"

void CRC_Init(void)
{
    RCC->AHB1ENR |= (1 << 12);  // enable CRC clock
    CRC->CR |= (1 << 0);        // reset
}

uint32_t CRC_Calculate(uint32_t* data, uint32_t length_in_words)
{
    CRC->CR |= (1 << 0);  // reset before each use

    for(uint32_t i = 0; i < length_in_words; i++)
        CRC->DR = data[i];

    return CRC->DR;
}
