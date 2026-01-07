#pragma once

#include "config.h"

/*
 * Packet structure
 *
 * CONSTRAINTS:
 *   - transmission_id must remain the FIRST field
 *   - length must remain the SECOND field
 *   - payload must remain the LAST field
 *   - Add new fields (sequence numbers, flags, etc.) between length and payload
 */
typedef struct packet {
    uint32_t transmission_id;           // Identifies the transmission (MUST BE FIRST)
    uint32_t length;                    // Payload byte count (MUST BE SECOND)
    uint8_t payload[MAX_PAYLOAD_SIZE];  // Payload data (MUST BE LAST)
} PACKET, *PPACKET;
