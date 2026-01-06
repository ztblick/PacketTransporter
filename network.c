/*
 * network.c
 * 
 * Network Layer Implementation
 * 
 * This layer provides a simple interface for sending and receiving packets.
 * It simulates an unreliable network—packets may be dropped, duplicated,
 * corrupted, or reordered depending on configuration settings.
 */

#include "application.h"

/*
 * Network buffers - two directional queues
 * TODO: Implement actual buffer structures with synchronization
 */


/*
 * network_init
 *
 * Initializes the network layer buffers and synchronization primitives.
 */
int network_init(void) {
    // TODO: Allocate and initialize Sender→Receiver buffer
    // TODO: Allocate and initialize Receiver→Sender buffer
    // TODO: Initialize synchronization primitives (mutexes, condition variables)

    return 0;  // Success
}


/*
 * network_cleanup
 *
 * Frees network layer resources.
 */
void network_cleanup(void) {
    // TODO: Free buffer memory
    // TODO: Destroy synchronization primitives
}


/*
 * send_packet
 *
 * Sends a packet through the simulated network.
 */
int send_packet(PPACKET pkt, int role) {
    // Validate inputs
    if (pkt == NULL) {
        return -1;
    }

    if (pkt->length > MAX_PAYLOAD_SIZE) {
        return -1;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return -1;
    }

    // Calculate actual bytes to transmit: header + payload
    size_t header_size = offsetof(PACKET, payload);
    size_t transmit_size = header_size + pkt->length;
    (void)transmit_size;  // Suppress unused warning for now

    // TODO: Apply network unreliability (drop, duplicate, corrupt, reorder)
    // TODO: Route packet to appropriate buffer based on role:
    //       ROLE_SENDER   → Sender→Receiver buffer
    //       ROLE_RECEIVER → Receiver→Sender buffer

    return 0;  // Accepted
}


/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
int receive_packet(struct packet* pkt, int timeout_ms, int role) {
    if (pkt == NULL || timeout_ms < 0) {
        return -1;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return -1;
    }

    // TODO: Wait up to timeout_ms for a packet from appropriate buffer:
    //       ROLE_SENDER   → Receiver→Sender buffer
    //       ROLE_RECEIVER → Sender→Receiver buffer
    // TODO: If packet arrives, copy to pkt and return 1
    // TODO: If timeout expires, return 0

    return 0;  // Timeout (stub)
}


/*
 * try_receive_packet
 *
 * Attempts to receive a packet without waiting.
 */
int try_receive_packet(PPACKET pkt, int role) {
    if (pkt == NULL) {
        return -1;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return -1;
    }

    // TODO: Check appropriate buffer for available packet:
    //       ROLE_SENDER   → Receiver→Sender buffer
    //       ROLE_RECEIVER → Sender→Receiver buffer
    // TODO: If packet available, copy to pkt and return 1
    // TODO: If no packet, return 0 immediately

    return 0;  // No packet (stub)
}