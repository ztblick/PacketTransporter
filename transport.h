/*
 * transport.h
 * 
 * Transport Layer API
 * 
 * Students implement these functions to provide reliable data transfer
 * over the unreliable network layer.
 */

#pragma once

#include "network.h"
#include "transport_receiver.h"
#include "transport_sender.h"

typedef struct {
    LIST_ENTRY list_entry;
    PPACKET packet;
} PACKET_LIST_ENTRY, *PPACKET_LIST_ENTRY;

typedef struct {
    PACKET_LIST_ENTRY entry;
    int list_length;
    CRITICAL_SECTION lock;

} PACKET_LIST_HEAD, *PPACKET_LIST_HEAD;

/*
 *  create_transport_layer
 *
 *  Allocates all necessary data for the transport layer.
 */
void create_transport_layer(void);

/*
 *  free_transport_layer
 *
 *  Frees all memory allocated by the transport layer.
 */
void free_transport_layer(void);

/**
 * send_transmission
 *
 * Reliably sends data to the receiver.
 *
 * Parameters:
 *   transmission_id - Unique identifier for this transmission (assigned by app layer)
 *   data            - Pointer to the data to send
 *   length          - Number of bytes to send
 *
 * Returns:
 *   0  - Success (all data acknowledged by receiver)
 *   1  - Failure (unrecoverable error, timeout, etc.)
 *
 * Notes:
 *   - Must break data into packets, each tagged with transmission_id
 *   - Should return only after transmission is complete or has failed
 *
 *
 *  When this function is called, we execute these steps in this order:
 *  1. We create a batch of packets from the transmission
 *  2. We send that batch
 *  3. We wait LATENCY_MS amount of time OR for the sender-listener to wake us
 *  4. We check our bitmap of ACKS
 *  5. If we need to re-send any packets, we will and we will repeat step 3.
 *  6. Otherwise, we move on to the next batch.
 *
 *
 */
#define TRANSMISSION_ACCEPTED       0
#define TRANSMISSION_REJECTED       1
int send_transmission(UINT32 transmission_id, PVOID data, SIZE_T length);


/*
 * receive_transmission
 *
 * Receives a completed transmission from the sender.
 *
 * Parameters:
 *   out_id     - Pointer where transmission_id will be written
 *   dest       - Pointer where reassembled data will be written
 *   out_length - Pointer where byte count will be written
 *   timeout_ms - Maximum time to wait for a completed transmission
 *
 * Returns:
 *   0  - Transmission received successfully (out_id, dest, out_length filled in)
 *   1  - Timeout (no transmission completed within timeout_ms)
 *
 * Notes:
 *   - Returns a completed transmission
 *   - Transmissions may complete in any order
 *   - Must track in-flight transmissions by transmission_id
 *   - Must reassemble packets into complete transmissions
 *   - Caller is responsible for providing a dest buffer large enough
 */
#define TRANSMISSION_RECEIVED       0
#define NO_TRANSMISSION_AVAILABLE   1
int receive_transmission(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms);