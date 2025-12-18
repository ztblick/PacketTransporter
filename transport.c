/*
 * transport.c
 *
 * Transport Layer Implementation (STUB)
 *
 * This file contains stub implementations for students to replace.
 * These stubs allow the project to compile and run, but do nothing useful.
 */

#include <stdint.h>
#include <stddef.h>
#include "transport.h"
#include "network.h"

/*
 * send_transmission (STUB)
 *
 * Students will implement reliable data transfer here.
 */
int send_transmission(uint32_t transmission_id, void* data, size_t length) {
    (void)transmission_id;  // Suppress unused parameter warning
    (void)data;
    (void)length;

    // TODO: Student implementation
    // - Break data into packets tagged with transmission_id
    // - Send packets via send_packet()
    // - Handle ACKs/NACKs from receiver
    // - Retransmit as needed

    return 0;  // Stub: pretend success
}


/*
 * receive_transmission (STUB)
 *
 * Students will implement packet reassembly here.
 */
int receive_transmission(uint32_t* out_id, void* dest, size_t* out_length, int timeout_ms) {
    (void)out_id;          // Suppress unused parameter warning
    (void)dest;
    (void)out_length;
    (void)timeout_ms;

    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Track multiple in-flight transmissions by transmission_id
    // - Reassemble packets into complete transmissions
    // - Send ACKs/NACKs to sender
    // - When complete, fill in out_id, dest, out_length and return 1

    return 0;  // Stub: always timeout
}