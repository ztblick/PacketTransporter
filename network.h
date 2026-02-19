/*
 * network.h
 *
 * Network Layer API
 *
 * Simulates an unreliable network channel between sender and receiver.
 * All communication passes through this layer, which can drop, duplicate,
 * corrupt, or reorder packets based on configuration.
 *
 * ============================================================================
 * PHYSICAL NETWORK SIMULATION
 * ============================================================================
 *
 * This layer simulates realistic network behavior, including:
 *
 * SERIALIZATION DELAY
 * -------------------
 * A physical network link can only transmit one bit at a time. The time to
 * "serialize" a packet onto the wire depends on packet size and link bandwidth:
 *
 *   serialization_delay = (packet_size_bits) / (bandwidth_bps)
 *
 * Example at 100 Mbps with a 1 KB packet:
 *
 *   1024 bytes * 8 bits/byte = 8,192 bits
 *   8,192 bits / 100,000,000 bits/sec = 81.92 microseconds
 *
 * While one packet is being serialized, other packets must wait. This is
 * enforced with a "wire mutex" - only one packet can be on the wire at a time.
 *
 * PROPAGATION DELAY
 * -----------------
 * After a packet is fully serialized onto the wire, it takes time to physically
 * travel to the destination. This is determined by the distance and the speed
 * of light in the medium (roughly 2/3 the speed of light in fiber/copper).
 *
 * We simulate this as a one-way latency: SIMULATED_LATENCY_MS / 2.
 *
 * Example with 20 ms round-trip time:
 *
 *   One-way propagation delay = 20 ms / 2 = 10 ms
 *
 * A packet "sent" at time T arrives at time T + serialization_delay + propagation_delay.
 *
 *
 * BANDWIDTH-DELAY PRODUCT (BDP)
 * -----------------------------
 * The BDP tells you how many bytes can be "in flight" (sent but not yet received)
 * at any moment:
 *
 *   BDP = bandwidth * round_trip_time
 *
 * Example at 100 Mbps with 20 ms RTT:
 *
 *   BDP = 100,000,000 bits/sec * 0.020 sec = 2,000,000 bits = 250,000 bytes = 250 KB
 *
 * With 1 KB packets, that's approximately 250 packets in flight.
 *
 *
 * SHARED LINK MODEL
 * -----------------
 * All connections share a single simulated link in each direction, just like
 * a real NIC. With 16 concurrent transmissions on a 100 Mbps link, each
 * effectively gets ~6.25 Mbps average throughput.
 *
 * ============================================================================
 * ARCHITECTURE
 * ============================================================================
 *
 * Two directional buffers carry packets between sender and receiver:
 *
 *     SENDER                                              RECEIVER
 *       |                                                    |
 *       |  send_packet(pkt, ROLE_SENDER)                     |
 *       | -------------------------------------------------> |
 *       |            Sender-to-Receiver Buffer               |
 *       |                (data packets)                      |
 *       |                                                    |  receive_packet(pkt, ..., ROLE_RECEIVER)
 *       |                                                    |
 *       |                                                    |  send_packet(pkt, ROLE_RECEIVER)
 *       | <------------------------------------------------- |
 *       |            Receiver-to-Sender Buffer               |
 *       |  receive_packet(pkt, ..., ROLE_SENDER)             |
 *       |                                                    |
 *
 * The `role` parameter determines which buffer is used:
 *
 *   - ROLE_SENDER sending    -> Sender-to-Receiver buffer
 *   - ROLE_RECEIVER sending  -> Receiver-to-Sender buffer
 *   - ROLE_SENDER receiving  -> Receiver-to-Sender buffer
 *   - ROLE_RECEIVER receiving -> Sender-to-Receiver buffer
 *
 * ============================================================================
 */

#pragma once

#include "utils.h"

/*
 * ============================================================================
 * SIMULATION PARAMETERS
 * ============================================================================
 */

/* Simulated link bandwidth in bits per second.
 * 100 Mbps is typical for fast broadband / local network.
 *
 * At 100 Mbps, a 1 KB packet takes ~82 microseconds to serialize.
 */
#define BANDWIDTH_BPS                    100000000

/* Simulated round-trip time in milliseconds.
 * 20 ms is typical for internet traffic (faster end).
 *
 * One-way propagation delay is half this value (10 ms).
 */
#define LATENCY_MS                        20

/* One-way propagation delay in milliseconds */
#define PROPAGATION_DELAY_MS             (LATENCY_MS / 2)

/* Bandwidth-delay product in bytes (how much data can be "in flight") */
#define BANDWIDTH_DELAY_PRODUCT_BYTES    ((BANDWIDTH_BPS / 8) * LATENCY_MS / 1000)

/* Maximum packets each metwork buffer can hold.
 * Packets are dropped when buffer is full (which should not happpen).
 */
#define NETWORK_BUFFER_CAPACITY_IN_BYTES       (MB(16))
#define NETWORK_BUFFER_SLOT_SIZE_IN_BYTES      (KB(1))
#define NETWORK_BUFFER_NUMBER_OF_SLOTS         ((NETWORK_BUFFER_CAPACITY_IN_BYTES + (NETWORK_BUFFER_SLOT_SIZE_IN_BYTES - 1)) / NETWORK_BUFFER_SLOT_SIZE_IN_BYTES)


// The default timeout for a network helper thread, in milliseconds
#define NET_RETRY_MS                      (5)
#define MAX_NIC_MISSES_BEFORE_SLEEP       (NIC_BUFFER_TOTAL_PACKET_SLOTS)

#define NO_BUFFER_SLOT_AVAILABLE          (NULL)
#define NO_BUFFER_PACKET_AVAILABLE        (NULL)

#define MAX_ATTEMPTS                      20

#define BITMAP_ROW_FULL_VALUE             MAXULONG64
#define TIMES_TO_SCAN_BITMAP_BEFORE_EXIT  1

/* ============================================================================
 * FUNCTIONS
 * ============================================================================*/

/*
 * create_network_layer
 *
 * Initializes the network layer. Must be called before any other network functions.
 *
 * Returns:
 *   void
 */
void create_network_layer(void);

/*
 * network_cleanup
 *
 * Cleans up network layer resources. Call when done.
 */
void free_network_layer(void);


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
 *   PACKET_ACCEPTED
 *   PACKET_REJECTED   invalid length, NULL pointer, or invalid role
 */
#define PACKET_ACCEPTED  0
#define PACKET_REJECTED  1
int send_packet(PPACKET pkt, int role);

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
 *   PACKET_RECEIVED         - Packet received successfully
 *   NO_PACKET_AVAILABLE     - Timeout (no packet arrived within timeout_ms)
 */
#define PACKET_RECEIVED         0
#define NO_PACKET_AVAILABLE     1
int receive_packet(PPACKET pkt, ULONG64 timeout_ms, int role);

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
 *   PACKET_RECEIVED      - Packet received successfully
 *   NO_PACKET_AVAILABLE  - No packet available
 */
int try_receive_packet(PPACKET pkt, int role);