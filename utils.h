//
// Created by zachb on 1/13/2026.
//

#pragma once

#include "config.h"

/**
 *   ===============================================================================
 *   ||   UNIVERSAL PACKET HEADER    ||   DATA / COMM HEADER   ||     PAYLOAD     ||
 *   ===============================================================================
 *
 *  UNIVERSAL PACKET HEADER
 *  The universal header can contain any number of fields, but it MUST begin with the size of the
 *  universal header (ULONG64). After that, it must include the transmission ID and the packet type.
 *  Finally, the total size of the payload (in bytes) is included.
 *  As packet structures grow and change over time, the struct can expand to hold more fields
 *  and minimal edits will need to be made to the code.
 *
 *  DATA / COMM HEADER
 *  Following the universal header is a header specifically for either a data packet or a comm packet.
 *  This also begins with the number of bytes in the header (ULONG64). Additional fields can follow.
 *
 *  PAYLOAD
 *  Finally, after the data or comm header is the payload.
 **/


/*
 * Universal packet header - shared by both packet types
 */
typedef struct universal_packet_header {
    ULONG64 total_bytes_in_packet_header;   // Describes the size of THIS header.
    UINT32 transmission_id : 31;            // Identifies which transmission this packet belongs to
    UINT32 packet_type : 1;                 // 0 = data packet, 1 = comm packet
    UINT32 bytes_in_payload;                // Identifies the number of bytes transmitted in the payload.

    // Additional fields may be added as packets expand over time.
    // Examples: error-correcting codes, version IDs, metadata for compression

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

#define PAGE_SIZE                        4096
#define PACKET_SIZE                      1024
// Thread handles for starting and ending simulation
HANDLE simulation_begin;
HANDLE simulation_end;

// Helper functions
PVOID zero_malloc(size_t bytes_to_allocate);
void time_init(void);
ULONG64 time_now_ms(void);
