//
// Created by zachb on 1/13/2026.
//

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
