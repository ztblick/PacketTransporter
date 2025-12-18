/*
 * network.h
 *
 * Network Layer API
 *
 * Provides a simulated unreliable network for sending and receiving packets.
 * Packets may be dropped, duplicated, corrupted, or reordered based on
 * configuration settings.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/*
 * Packet structure - students extend this by adding fields between length and payload
 *
 * CONSTRAINTS:
 *   - transmission_id must remain the FIRST field
 *   - length must remain the SECOND field
 *   - payload must remain the LAST field
 *   - Add new fields (sequence numbers, flags, etc.) between length and payload
 */
struct packet {
    uint32_t transmission_id;           // Identifies the transmission (MUST BE FIRST)
    uint32_t length;                    // Payload byte count (MUST BE SECOND)
    uint8_t payload[MAX_PAYLOAD_SIZE];  // Payload data (MUST BE LAST)
};

/*
 * network_init
 *
 * Initializes the network layer. Must be called before any other network functions.
 *
 * Returns:
 *   0  - Success
 *  -1  - Failure
 */
int network_init(void);

/*
 * network_cleanup
 *
 * Cleans up network layer resources. Call when done.
 */
void network_cleanup(void);

/*
 * send_packet
 *
 * Sends a packet through the simulated network.
 *
 * Parameters:
 *   pkt  - Pointer to the packet to send
 *   role - ROLE_SENDER or ROLE_RECEIVER (identifies the caller)
 *
 * Returns:
 *   0  - Packet accepted
 *  -1  - Packet rejected (invalid length, NULL pointer, or invalid role)
 */
int send_packet(struct packet* pkt, int role);

/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 *
 * Parameters:
 *   pkt        - Pointer to packet struct where received data will be written
 *   timeout_ms - Maximum time to wait for a packet (milliseconds)
 *   role       - ROLE_SENDER or ROLE_RECEIVER (identifies the caller)
 *
 * Returns:
 *   1  - Packet received successfully
 *   0  - Timeout (no packet arrived within timeout_ms)
 *  -1  - Error
 */
int receive_packet(struct packet* pkt, int timeout_ms, int role);

/*
 * try_receive_packet
 *
 * Attempts to receive a packet without waiting.
 *
 * Parameters:
 *   pkt  - Pointer to packet struct where received data will be written
 *   role - ROLE_SENDER or ROLE_RECEIVER (identifies the caller)
 *
 * Returns:
 *   1  - Packet received successfully
 *   0  - No packet available
 *  -1  - Error
 */
int try_receive_packet(struct packet* pkt, int role);

#endif /* NETWORK_H */