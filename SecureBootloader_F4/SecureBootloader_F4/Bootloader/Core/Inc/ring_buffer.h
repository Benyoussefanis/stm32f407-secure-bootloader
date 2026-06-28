#ifndef RING_BUFFER_H
#define RING_BUFFER_H
#include <stdint.h>

#define RX_BUFFER_SIZE  256

typedef struct {
    uint8_t  data[RX_BUFFER_SIZE];
    uint16_t head;
    uint16_t tail;
} RingBuffer_t;

void    RingBuffer_Init(RingBuffer_t* rb);
void    RingBuffer_Write(RingBuffer_t* rb, uint8_t byte);
uint8_t RingBuffer_Read(RingBuffer_t* rb, uint8_t* byte);
uint8_t RingBuffer_IsEmpty(RingBuffer_t* rb);

#endif
