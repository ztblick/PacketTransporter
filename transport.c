/*
 * transport.c
 *
 * Transport Layer Implementation 
 *
 * This file contains functions for you to implement!
 * These functions allow the project to compile and run, but do nothing useful.
 */

#include "transport.h"
#include "sender/sender.c"

void create_transport_layer(void) {
    create_sender();
    return;
}

void free_transport_layer(void) {
    return;
}

int send_transmission(UINT32 transmission_id, PVOID data, SIZE_T length)
{
    // TODO: Student implementation
    // - Break data into packets tagged with transmission_id
    // - Send packets via send_packet()


    PSENDER_TRANSMISSION_INFO current_transmission = &g_sender_state.transmissions_in_progress[transmission_id];
    current_transmission->data = data;

    ULONG64 num_packets = (length + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    current_transmission->number_of_packets_in_transmission = num_packets;
    current_transmission->packet_status_bitmap = zero_malloc((num_packets + 7) / 8);
    current_transmission->total_bytes = length;
    current_transmission->sending_complete_event = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Add the transmission ID to the work array
    ULONG64 write_index = g_sender_state.transmissions_queue.next_write_index % WORK_ARRAY_SIZE;
    g_sender_state.transmissions_queue.work_array[write_index] = transmission_id;

    // Increase the write index (How many IDs we have written in TOTAL to the transmission queue, this is
    // different from the index we are on in get_next_transmission_id)
    g_sender_state.transmissions_queue.next_write_index++;


    WaitForSingleObject(current_transmission->sending_complete_event, INFINITE);
    
    return TRANSMISSION_ACCEPTED;
}

int receive_transmission(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms) {

    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Reassemble packets into complete transmissions
    // - When complete, fill in out_id, dest, out_length and return 1

    return NO_TRANSMISSION_AVAILABLE;
}