/*
 * transport.c
 *
 * Transport Layer Implementation 
 *
 * This file contains functions for you to implement!
 * These functions allow the project to compile and run, but do nothing useful.
 */

#include "transport.h"
#include "transport_sender.h"

void create_transport_layer(void) {
    create_sender();
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

VOID create_sender(VOID)
{
    // Create sender listener thread.
    CreateThread(NULL, 0, sender_listener, NULL, 0, NULL);


    // Create our minion threads.
    for (int i = 0; i < SENDER_MINION_COUNT; i++) {
        CreateThread(NULL, 0, sender_minion, NULL, 0, NULL);
    }
}

VOID packetize_contiguous(PVOID transmission_data, ULONG64 bytes_to_packetize)
{

}

VOID send_packet_batch(ULONG64 number_of_packets_to_send)
{

}


DWORD sender_listener(LPVOID param)
{

}

DWORD sender_minion(LPVOID param)
{

}

VOID find_work(VOID)
{
    
}

int receive_transmission(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms) {

    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Reassemble packets into complete transmissions
    // - When complete, fill in out_id, dest, out_length and return 1

    return NO_TRANSMISSION_AVAILABLE;
}