/*
 * config.h
 * 
 * Configuration settings for the network simulation.
 * Modify these values to change network behavior and reliability.
 */

#pragma once

#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include "debug.h"

// Maximum number of bytes per packet
#define MAX_PAYLOAD_SIZE        1024

// Default timeout for receive_packet (milliseconds)
#define PACKET_WAIT_TIME_MS     500

/* Role identifiers - pass to send_packet/receive_packet/try_receive_packet */
#define ROLE_SENDER             0
#define ROLE_RECEIVER           1

/* 
 * Network reliability settings
 * Set to 0 to disable, or a value 1-100 representing percentage chance
 */
#define NETWORK_DROP_RATE       0   // Percentage of packets silently dropped
#define NETWORK_DUPLICATE_RATE  0   // Percentage of packets sent twice
#define NETWORK_CORRUPT_RATE    0   // Percentage of packets with corrupted data

/*
 * Set to 1 to enable packet reordering (packets may arrive out of order)
 * Set to 0 for in-order delivery
 */
#define NETWORK_REORDER_ENABLED 0