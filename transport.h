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

/*
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