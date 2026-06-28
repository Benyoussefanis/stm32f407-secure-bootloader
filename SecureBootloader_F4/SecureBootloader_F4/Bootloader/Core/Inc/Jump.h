#ifndef JUMP_H
#define JUMP_H
#include <stdint.h>
#define APP_ADDRESS   0x08008000UL
void Jump_ToApplication(uint32_t app_address);
#endif
