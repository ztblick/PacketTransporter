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
int send_transmission(uint32_t transmission_id, void* data, size_t length);


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
 *   2  - Error
 *
 * Notes:
 *   - Returns a completed transmission
 *   - Transmissions may complete in any order
 *   - Must track in-flight transmissions by transmission_id
 *   - Must reassemble packets into complete transmissions
 *   - Caller is responsible for providing a dest buffer large enough
 */
int receive_transmission(uint32_t* out_id, void* dest, size_t* out_length, int timeout_ms);