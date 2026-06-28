#include "ring_buffer.h"

void RingBuffer_Init(RingBuffer_t* rb)
{
    rb->head = 0;
    rb->tail = 0;
}

void RingBuffer_Write(RingBuffer_t* rb, uint8_t byte)
{
    uint16_t next = (rb->head + 1) % RX_BUFFER_SIZE;
    if(next != rb->tail)
    {
        rb->data[rb->head] = byte;
        rb->head = next;
    }
}

uint8_t RingBuffer_Read(RingBuffer_t* rb, uint8_t* byte)
{
    if(rb->head == rb->tail)
        return 0;
    *byte = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % RX_BUFFER_SIZE;
    return 1;
}

uint8_t RingBuffer_IsEmpty(RingBuffer_t* rb)
{
    return (rb->head == rb->tail);
}
