#ifndef USART_H
#define USART_H

#include <stdint.h>
#include "ring_buffer.h"

extern RingBuffer_t rx_buf;

void    USART2_Init(void);
void    USART2_SendByte(uint8_t byte);
void    USART2_SendBuffer(uint8_t* buf, uint16_t len);

#endif
