#include "protocol.h"
#include "usart.h"
#include "crc.h"
#include <string.h>

typedef enum {
    PARSE_SYNC1,
    PARSE_SYNC2,
    PARSE_TYPE,
    PARSE_SEQ,
    PARSE_LEN_H,
    PARSE_LEN_L,
    PARSE_DATA,
    PARSE_CRC
} ParseState_t;

static ParseState_t state    = PARSE_SYNC1;
static Packet_t     current;
static uint8_t      raw[300];
static uint16_t     raw_idx  = 0;
static uint16_t     data_idx = 0;
static uint8_t      crc_buf[4];
static uint8_t      crc_idx  = 0;

uint8_t Protocol_FeedByte(uint8_t byte, Packet_t* pkt)
{
    switch(state)
    {
        case PARSE_SYNC1:
            if(byte == SYNC_1)
            {
                raw_idx = 0;
                raw[raw_idx++] = byte;
                state = PARSE_SYNC2;
            }
            break;

        case PARSE_SYNC2:
            raw[raw_idx++] = byte;
            if(byte == SYNC_2)
                state = PARSE_TYPE;
            else
            {
                raw_idx = 0;
                state = PARSE_SYNC1;
            }
            break;

        case PARSE_TYPE:
            current.type = byte;
            raw[raw_idx++] = byte;
            state = PARSE_SEQ;
            break;

        case PARSE_SEQ:
            current.seq = byte;
            raw[raw_idx++] = byte;
            state = PARSE_LEN_H;
            break;

        case PARSE_LEN_H:
            current.length = (uint16_t)byte << 8;
            raw[raw_idx++] = byte;
            state = PARSE_LEN_L;
            break;

        case PARSE_LEN_L:
            current.length |= byte;
            raw[raw_idx++] = byte;
            data_idx = 0;
            if(current.length == 0)
            {
                crc_idx = 0;
                state = PARSE_CRC;
            }
            else
                state = PARSE_DATA;
            break;

        case PARSE_DATA:
            if(data_idx < MAX_DATA_SIZE)
                current.data[data_idx] = byte;
            raw[raw_idx++] = byte;
            data_idx++;
            if(data_idx >= current.length)
            {
                crc_idx = 0;
                state = PARSE_CRC;
            }
            break;

        case PARSE_CRC:
            crc_buf[crc_idx++] = byte;
            if(crc_idx >= 4)
            {
                uint32_t crc_recv =
                    (uint32_t)crc_buf[0]        |
                    (uint32_t)crc_buf[1] << 8   |
                    (uint32_t)crc_buf[2] << 16  |
                    (uint32_t)crc_buf[3] << 24;

                // pad raw to word boundary
                while(raw_idx % 4 != 0)
                    raw[raw_idx++] = 0x00;

                uint32_t crc_calc = CRC_Calculate(
                    (uint32_t*)raw, raw_idx / 4);

                // reset before returning
                state    = PARSE_SYNC1;
                raw_idx  = 0;
                data_idx = 0;
                crc_idx  = 0;

                if(crc_calc == crc_recv)
                {
                    *pkt = current;
                    return PACKET_VALID;
                }
                else
                    return PACKET_CRC_ERROR;
            }
            break;
    }
    return PACKET_INCOMPLETE;
}

void Protocol_SendACK(uint8_t seq)
{
    uint8_t pkt[10];
    pkt[0] = SYNC_1;
    pkt[1] = SYNC_2;
    pkt[2] = PKT_ACK;
    pkt[3] = seq;
    pkt[4] = 0x00;
    pkt[5] = 0x00;

    uint8_t crc_input[8];
    memcpy(crc_input, pkt, 6);
    crc_input[6] = 0x00;
    crc_input[7] = 0x00;

    uint32_t crc = CRC_Calculate((uint32_t*)crc_input, 2);
    pkt[6] = (crc >> 0)  & 0xFF;
    pkt[7] = (crc >> 8)  & 0xFF;
    pkt[8] = (crc >> 16) & 0xFF;
    pkt[9] = (crc >> 24) & 0xFF;

    USART2_SendBuffer(pkt, 10);
}

void Protocol_SendNACK(uint8_t seq)
{
    uint8_t pkt[10];
    pkt[0] = SYNC_1;
    pkt[1] = SYNC_2;
    pkt[2] = PKT_NACK;
    pkt[3] = seq;
    pkt[4] = 0x00;
    pkt[5] = 0x00;

    uint8_t crc_input[8];
    memcpy(crc_input, pkt, 6);
    crc_input[6] = 0x00;
    crc_input[7] = 0x00;

    uint32_t crc = CRC_Calculate((uint32_t*)crc_input, 2);
    pkt[6] = (crc >> 0)  & 0xFF;
    pkt[7] = (crc >> 8)  & 0xFF;
    pkt[8] = (crc >> 16) & 0xFF;
    pkt[9] = (crc >> 24) & 0xFF;

    USART2_SendBuffer(pkt, 10);
}
