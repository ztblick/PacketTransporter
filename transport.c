/*
 * transport.c
 *
 * Transport Layer Implementation 
 *
 * This file contains functions for you to implement!
 * These functions allow the project to compile and run, but do nothing useful.
 */

#include "transport.h"

void create_transport_layer(void) {
    return;
}

void free_transport_layer(void) {
    return;
}

int send_transmission(uint32_t transmission_id, void* data, size_t length) {

    // TODO: Student implementation
    // - Break data into packets tagged with transmission_id
    // - Send packets via send_packet()
    
    return 0;
}


int receive_transmission(uint32_t* out_id, void* dest, size_t* out_length, int timeout_ms) {

    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Reassemble packets into complete transmissions
    // - When complete, fill in out_id, dest, out_length and return 1

    return 0;
}