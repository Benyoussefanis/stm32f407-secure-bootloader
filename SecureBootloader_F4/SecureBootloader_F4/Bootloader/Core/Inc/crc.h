// crc.h
#ifndef CRC_H
#define CRC_H

#include <stdint.h>

void     CRC_Init(void);
uint32_t CRC_Calculate(uint32_t* data, uint32_t length_in_words);

#endif
