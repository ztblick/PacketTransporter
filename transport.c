/*
 * transport.c
 *
 * Transport Layer Implementation 
 *
 * This file contains functions for you to implement!
 * These functions allow the project to compile and run, but do nothing useful.
 */

#include "transport.h"
#include "transport_receiver.h"

RECEIVER_STATE g_receiver_state;


void create_transport_layer(void) {
    return;
}

void free_transport_layer(void) {
    return;
}

int send_transmission(UINT32 transmission_id, PVOID data, SIZE_T length) {

    // TODO: Student implementation
    // - Break data into packets tagged with transmission_id
    // - Send packets via send_packet()
    
    return TRANSMISSION_ACCEPTED;
}


int receive_transmission(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms) {
    return reciever_handler(transmission_id, dest, out_length, timeout_ms);
}