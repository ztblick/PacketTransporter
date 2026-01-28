//
// Created by zachb on 1/13/2026.
//

#pragma once

#include "config.h"

/*
 * Common packet header - shared by both packet types
 */
typedef struct packet_header {
    UINT32 transmission_id : 31;  // Identifies which transmission this packet belongs to
    UINT32 packet_type : 1;       // 0 = data packet, 1 = comm packet
    UINT32 bytes_sent;            // Bytes in payload (data) or bitmaps (comm)
} PACKET_HEADER;

/*
 * Data Packet -- contains transmission data and metadata.
 */
typedef struct data_packet {
    UINT32 transmission_id : 31;      // Indicates which transmission this packet belongs to.
    UINT32 must_be_zero : 1;          // When this bit is cleared, we interpret the packet as a data packet.
    UINT32 bytes_in_payload;          // Documents how many bytes in the payload are relevant.
                                      // This must be > 0 and < MAX_PAYLOAD_SIZE.

    UINT32 index_in_transmission;     // Indicates the packet's position in the transmission (e.g. packet #3/5)
    UINT32 n_packets_in_transmission; // Contains the total number of packets in this transmission.

    BYTE payload[MAX_PAYLOAD_SIZE];   // Contains the actual data to be transmitted.
} DATA_PACKET, *PDATA_PACKET;

/*
 *  Comm Packet -- contains ACK bitmaps.
 */
typedef struct comm_packet {
    UINT32 transmission_id : 31;      // Indicates which transmission we are acknowledging.
    UINT32 must_be_one : 1;           // When this bit is set, we interpret the packet as a comm packet.
    UINT32 bitmaps_sent;              // Documents how many of the 1-byte bitmaps contain ACKs
                                      // This must be > 0 and < MAX_PAYLOAD_SIZE.

    UINT32 reserved_0;                // This memory is not used for a comm packet yet, but it is allocated
    UINT32 reserved_1;                // so the overlay of the two packet structs remains consistent.

    BYTE bitmaps[MAX_PAYLOAD_SIZE];   // Contains bitmaps, where each 0 indicates a packet that WAS NOT
                                      // received and a 1 indicates a packet that WAS received.
} COMM_PACKET, *PCOMM_PACKET;

/*
 *  Generic packet struct -- used by the network layer.
 */
typedef union packet {
    PACKET_HEADER header;
    DATA_PACKET data;
    COMM_PACKET comm;
} PACKET, *PPACKET;

// Timing variables
LARGE_INTEGER perf_frequency;
LARGE_INTEGER time_start;

// Thread initialization information
#define DEFAULT_SECURITY                ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE              0
#define DEFAULT_CREATION_FLAGS          0
#define AUTO_RESET                      FALSE
#define MANUAL_RESET                    TRUE

#define EXIT_EVENT_INDEX                0
#define ACTIVE_EVENT_INDEX              1

// Thread handles for starting and ending simulation
HANDLE simulation_begin;
HANDLE simulation_end;

// Helper functions
PVOID zero_malloc(size_t bytes_to_allocate);
void time_init(void);
ULONG64 time_now_ms(void);
