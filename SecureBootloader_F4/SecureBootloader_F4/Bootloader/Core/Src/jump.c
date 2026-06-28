#include "jump.h"
#include "stm32f4xx.h"

uint8_t Jump_IsApplicationValid(void)
{
    uint32_t sp = *(uint32_t*)APP_ADDRESS;
    return (sp >= 0x20000000 && sp <= 0x20020000);
}

void Jump_ToApplication(uint32_t app_address)
{
    uint32_t app_sp = *(volatile uint32_t*)(app_address);
    uint32_t app_pc = *(volatile uint32_t*)(app_address + 4);

    if ((app_sp & 0xFF000000) != 0x20000000) return;
    if ((app_pc & 0xFF000000) != 0x08000000) return;

    __disable_irq();
    NVIC_DisableIRQ(USART2_IRQn);
    SysTick->CTRL = 0;

    SCB->VTOR = app_address;
    __set_MSP(app_sp);

    void (*app_reset)(void) = (void (*)(void))(app_pc);
    app_reset();
}
