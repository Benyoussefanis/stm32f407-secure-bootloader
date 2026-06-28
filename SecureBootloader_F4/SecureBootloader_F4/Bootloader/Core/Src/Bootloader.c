#include "bootloader.h"
#include "flash.h"
#include "jump.h"
#include "usart.h"
#include "protocol.h"
#include "crc.h"
#include "ring_buffer.h"
#include "stm32f4xx.h"
#include <string.h>
#include "sha256.h"
#include "uECC.h"
#include "pubkey.h"
#include "aes_gcm.h"

static uint8_t update_requested(void)
{
    RCC->APB1ENR |= (1 << 28);
    PWR->CR |= (1 << 8);
    volatile uint32_t t = 10000;
    while (!(PWR->CR & (1 << 8)) && t--);
    uint32_t magic = RTC->BKP0R;
    return (magic == UPDATE_MAGIC);
}

// key = SHA256(device UID + nonce), first 16 bytes
static void make_session_key(const uint8_t *nonce, uint8_t *key_out)
{
    uint8_t *uid = (uint8_t*)0x1FFF7A10;  // 12 byte factory UID on STM32F4
    uint8_t key_input[24];
    memcpy(key_input,      uid,   12);
    memcpy(key_input + 12, nonce, 12);
    SHA256_CTX ctx;
    uint8_t hash_buf[32];
    sha256_init(&ctx);
    sha256_update(&ctx, key_input, 24);
    sha256_final(&ctx, hash_buf);
    memcpy(key_out, hash_buf, 16);
}

static uint8_t check_app(void)
{
    // first word of the firmware should be the stack pointer
    // if it points to RAM its probably a real app
    uint32_t sp = *(volatile uint32_t*)APP_ADDRESS;
    return ((sp & 0xFF000000) == 0x20000000);
}

// copies the downloaded firmware from slot 6 to the app area
static void install_firmware(uint32_t image_size)
{
    Flash_Unlock();
    for (uint8_t s = APP_SECTOR_FIRST; s <= APP_SECTOR_LAST; s++)
        Flash_EraseSector(s);

    uint8_t  *src = (uint8_t*)DOWNLOAD_SLOT_ADDR;
    uint32_t  dst = APP_ADDRESS;
    uint32_t  remaining = image_size;
    while (remaining)
    {
        uint32_t n = (remaining > 256) ? 256 : remaining;
        Flash_WriteBuffer(dst, src, n);
        dst       += n;
        src       += n;
        remaining -= n;
    }
    Flash_Lock();
}

static uint32_t calc_slot_crc(uint32_t image_size)
{
    uint32_t words = (image_size + 3) / 4;
    return CRC_Calculate((uint32_t*)DOWNLOAD_SLOT_ADDR, words);
}

// store: [magic 4 bytes][version 4 bytes]
// magic helps detect if the sector was never written or got corrupted
static uint32_t version_read(void)
{
    uint32_t magic = *(volatile uint32_t*)VERSION_STORE_ADDR;
    uint32_t v     = *(volatile uint32_t*)(VERSION_STORE_ADDR + 4);
    if (magic != VERSION_MAGIC) return 0;
    return v;
}

static void version_write(uint32_t v)
{
    uint8_t buf[8];
    uint32_t magic = VERSION_MAGIC;
    buf[0] = (magic      ) & 0xFF;
    buf[1] = (magic >>  8) & 0xFF;
    buf[2] = (magic >> 16) & 0xFF;
    buf[3] = (magic >> 24) & 0xFF;
    buf[4] = (v      ) & 0xFF;
    buf[5] = (v >>  8) & 0xFF;
    buf[6] = (v >> 16) & 0xFF;
    buf[7] = (v >> 24) & 0xFF;
    Flash_Unlock();
    Flash_EraseSector(VERSION_STORE_SECTOR);
    Flash_WriteBuffer(VERSION_STORE_ADDR, buf, 8);
    Flash_Lock();
}

static void do_update(void)
{
    extern RingBuffer_t rx_buf;

    uint8_t  session_key[16] = {0};
    uint8_t  nonce[12]       = {0};
    uint32_t new_version     = 0;
    uint8_t  byte;
    Packet_t pkt;
    uint8_t  sig[64] = {0};

    RTC->BKP0R = 0;
    NVIC_EnableIRQ(USART2_IRQn);

    uint8_t msg[] = "UPDATE MODE\r\n";
    USART2_SendBuffer(msg, sizeof(msg) - 1);

    GPIOD->ODR |= (1 << 13);  // orange LED on

    uint32_t write_addr   = DOWNLOAD_SLOT_ADDR;
    uint32_t image_size   = 0;
    uint32_t expected_crc = 0;
    uint8_t  erased       = 0;
    uint8_t  fw_ready     = 0;
    uint8_t  seq_expected = 0;

    while (!fw_ready)
    {
        if (!RingBuffer_Read(&rx_buf, &byte)) continue;

        uint8_t result = Protocol_FeedByte(byte, &pkt);

        if (result == PACKET_CRC_ERROR) {
            Protocol_SendNACK(pkt.seq);
            continue;
        }
        if (result != PACKET_VALID) continue;

        Protocol_SendACK(pkt.seq);

        switch (pkt.type)
        {
            case PKT_HANDSHAKE:
                write_addr   = DOWNLOAD_SLOT_ADDR;
                image_size   = 0;
                erased       = 0;
                seq_expected = 1;
                // send the device UID so the PC can derive the session key
                {
                    uint8_t *uid = (uint8_t*)0x1FFF7A10;
                    USART2_SendBuffer(uid, 12);
                }
                break;

            case PKT_UPDATE_START:
                if (pkt.length >= 12)
                    memcpy(nonce, pkt.data, 12);
                make_session_key(nonce, session_key);
                GPIOD->ODR |= (1 << 15);
                Flash_Unlock();
                Flash_EraseSector(DOWNLOAD_SLOT_SECTOR);  // wipe slot 6
                Flash_Lock();
                GPIOD->ODR &= ~(1 << 15);
                erased     = 1;
                write_addr = DOWNLOAD_SLOT_ADDR;
                image_size = 0;
                break;

            case PKT_DATA:
                if (!erased) break;
                if (pkt.seq != seq_expected) break;
                if (pkt.length < 17) break;  // need at least 1 byte data + 16 byte tag
                {
                    uint8_t  chunk_iv[12];
                    uint16_t ct_len = pkt.length - 16;
                    uint8_t *ct     = pkt.data;
                    uint8_t *tag    = pkt.data + ct_len;

                    // IV changes per chunk using the sequence number
                    // this prevents reusing the same IV with the same key
                    memcpy(chunk_iv, nonce, 12);
                    chunk_iv[8]  ^= (pkt.seq >> 24) & 0xFF;
                    chunk_iv[9]  ^= (pkt.seq >> 16) & 0xFF;
                    chunk_iv[10] ^= (pkt.seq >>  8) & 0xFF;
                    chunk_iv[11] ^= (pkt.seq      ) & 0xFF;

                    int gcm_result = aes_gcm_decrypt(session_key, chunk_iv,
                                                     NULL, 0,
                                                     ct, ct_len, tag);
                    if (gcm_result != 0) {
                        Protocol_SendNACK(pkt.seq);
                        break;
                    }

                    Flash_Unlock();
                    Flash_WriteBuffer(write_addr, ct, ct_len);
                    Flash_Lock();

                    GPIOD->ODR ^= (1 << 15);  // blink blue
                    write_addr += ct_len;
                    image_size += ct_len;
                    seq_expected++;
                }
                break;

            case PKT_UPDATE_DONE:
                if (pkt.length >= 4)
                    expected_crc = (uint32_t)pkt.data[0]        |
                                   (uint32_t)pkt.data[1] << 8   |
                                   (uint32_t)pkt.data[2] << 16  |
                                   (uint32_t)pkt.data[3] << 24;
                if (pkt.length >= 8)
                    new_version  = (uint32_t)pkt.data[4]        |
                                   (uint32_t)pkt.data[5] << 8   |
                                   (uint32_t)pkt.data[6] << 16  |
                                   (uint32_t)pkt.data[7] << 24;
                if (pkt.length >= 72)
                    memcpy(sig, &pkt.data[8], 64);
                fw_ready = 1;
                break;
        }
    }

    GPIOD->ODR &= ~(1 << 13);
    GPIOD->ODR &= ~(1 << 15);

    // run all checks before touching the app
    uint32_t actual_crc = calc_slot_crc(image_size);
    uint32_t sp_slot    = *(volatile uint32_t*)DOWNLOAD_SLOT_ADDR;

    uint8_t crc_ok  = (actual_crc == expected_crc);
    uint8_t sp_ok   = ((sp_slot & 0xFF000000) == 0x20000000);
    uint8_t ver_ok  = (new_version >= version_read());  // >= so same version is allowed

    // hash the whole image and check the ECDSA signature
    uint8_t hash[32];
    SHA256_CTX sha_ctx;
    sha256_init(&sha_ctx);
    sha256_update(&sha_ctx, (uint8_t*)DOWNLOAD_SLOT_ADDR, image_size);
    sha256_final(&sha_ctx, hash);

    int     ecdsa_result = uECC_verify(PUBLIC_KEY, hash, 32, sig, uECC_secp256r1());
    uint8_t ecdsa_ok     = (ecdsa_result == 1);

    if (crc_ok && sp_ok && ver_ok && ecdsa_ok)
    {
        install_firmware(image_size);
        version_write(new_version);

        uint8_t ok_msg[] = "INSTALL OK\r\n";
        USART2_SendBuffer(ok_msg, sizeof(ok_msg) - 1);
        GPIOD->ODR |= (1 << 12);  // green
        for (uint8_t i = 0; i < 6; i++) {
            GPIOD->ODR ^= (1 << 15);
            HAL_Delay(150);
        }
        GPIOD->ODR &= ~(1 << 15);
        HAL_Delay(1000);
        NVIC_SystemReset();
    }
    else
    {
        if (!ver_ok) {
            uint8_t rb_msg[] = "ROLLBACK BLOCKED\r\n";
            USART2_SendBuffer(rb_msg, sizeof(rb_msg) - 1);
        } else {
            uint8_t fail_msg[] = "VERIFY FAILED\r\n";
            USART2_SendBuffer(fail_msg, sizeof(fail_msg) - 1);
        }
        GPIOD->ODR |= (1 << 14);  // red
        HAL_Delay(2000);
        NVIC_SystemReset();
    }
}

static uint8_t button_held(void)
{
    RCC->AHB1ENR |= (1 << 0);
    GPIOA->MODER &= ~(0x3 << 0);
    RCC->APB1ENR |= (1 << 28);
    __DSB();
    PWR->CR |= (1 << 8);
    return ((GPIOA->IDR) & (1 << 0));
}

void Bootloader_Run(void)
{
    if (button_held()) {
        RTC->BKP0R = 0xDEADC0DE;
    }
    if (update_requested()) {
        do_update();
    }
    if (check_app()) {
        GPIOD->ODR |= (1 << 14);
        HAL_Delay(1000);
        GPIOD->ODR &= ~(1 << 14);
        Jump_ToApplication(APP_ADDRESS);
    }
    do_update();
}
