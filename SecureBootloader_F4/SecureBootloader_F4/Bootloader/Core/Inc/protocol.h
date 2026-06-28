#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>

#define SYNC_1             0xAA
#define SYNC_2             0x55
#define PKT_HANDSHAKE      0x01
#define PKT_UPDATE_START   0x04
#define PKT_DATA           0x05
#define PKT_ACK            0x06
#define PKT_NACK           0x07
#define PKT_UPDATE_DONE    0x08
#define MAX_DATA_SIZE      256
#define PACKET_INCOMPLETE  0
#define PACKET_VALID       1
#define PACKET_CRC_ERROR   2

typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint16_t length;
    uint8_t  data[MAX_DATA_SIZE];
} Packet_t;

uint8_t Protocol_FeedByte(uint8_t byte, Packet_t* pkt);
void    Protocol_SendACK(uint8_t seq);
void    Protocol_SendNACK(uint8_t seq);
#endif
