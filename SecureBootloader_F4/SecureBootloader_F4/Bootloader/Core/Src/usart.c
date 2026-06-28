#include "usart.h"
#include "stm32f4xx.h"

RingBuffer_t rx_buf;

void USART2_Init(void)
{
    RingBuffer_Init(&rx_buf);

    RCC->APB1ENR |= (1 << 17);  // USART2
    RCC->AHB1ENR |= (1 << 0);   // GPIOA

    // PA2 = TX, PA3 = RX, both set to alternate function
    GPIOA->MODER &= ~(0x3 << (2*2));
    GPIOA->MODER |=  (0x2 << (2*2));
    GPIOA->MODER &= ~(0x3 << (2*3));
    GPIOA->MODER |=  (0x2 << (2*3));

    // AF7 is USART2 on these pins
    GPIOA->AFR[0] &= ~(0xF << (2*4));
    GPIOA->AFR[0] |=  (0x7 << (2*4));
    GPIOA->AFR[0] &= ~(0xF << (3*4));
    GPIOA->AFR[0] |=  (0x7 << (3*4));

    USART2->CR1 &= ~(1 << 12);   // 8 bit
    USART2->CR1 &= ~(1 << 10);   // no parity
    USART2->CR2 &= ~(0x3 << 12); // 1 stop bit

    // 115200 baud at 42MHz APB1
    // USARTDIV = 42000000 / (16 * 115200) = 22.786 -> mantissa=22, frac=13
    USART2->BRR = (22 << 4) | (13 << 0);

    USART2->CR1 |= (1 << 5);   // RXNE interrupt enable
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);

    USART2->CR1 |= (1 << 3);   // TX enable
    USART2->CR1 |= (1 << 2);   // RX enable
    USART2->CR1 |= (1 << 13);  // USART enable (always last)
}

void USART2_SendByte(uint8_t byte)
{
    while(!(USART2->SR & (1 << 7)));
    USART2->DR = byte;
}

void USART2_SendBuffer(uint8_t* buf, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
        USART2_SendByte(buf[i]);
}

void USART2_IRQHandler(void)
{
    if(USART2->SR & (1 << 5))       // RXNE flag
    {
        uint8_t byte = USART2->DR;  // reading DR clears the flag
        RingBuffer_Write(&rx_buf, byte);
    }
}
