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
#include "transport_receiver.h"

RECEIVER_STATE g_receiver_state;

void create_transport_layer(void) {
    create_receiver();
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

    printf("Sending transmission %d length %llu\n", transmission_id, length);


    PSENDER_TRANSMISSION_INFO current_transmission = &g_sender_state.transmissions_in_progress[transmission_id];


    if (VirtualAlloc(current_transmission, sizeof(SENDER_TRANSMISSION_INFO), MEM_COMMIT, PAGE_READWRITE) == NULL) {
        DebugBreak();
    }

    current_transmission->data = data;

    ULONG64 num_packets = (length + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    current_transmission->number_of_packets_in_transmission = num_packets;
    current_transmission->packet_status_bitmap = zero_malloc((num_packets + 63) / 64 * sizeof(UINT64));
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
    int returnVal = reciever_handler(transmission_id, dest, out_length, timeout_ms);

    printf("Received transmission %d length %llu\n", transmission_id, *out_length);

    return returnVal;
}
BYTE write_to_cache(PDATA_PACKET Niko_Packet) {
    printf(".");
    // Make sure packet exists/if Niko does a bad job
    ASSERT(Niko_Packet);
    // Attempt to reserve slot in cache to write into
    boolean found_slot = FALSE;
    int attempts = 0;
    int return_value = 0;
    int chunk = 0;
    int offset = 0;
//    int slot = 0;
    int original_value_counter = 0;
    // Continue looking for a slot in the cache to write in
    while (found_slot == FALSE) {
        // Take a "snapshot" / save original value of slot counter so that we can do consistent math even if other
        // threads increment slot_counter
        original_value_counter = g_receiver_state.packet_cache.slot_counter_writer;
        // To find the correct slot in any chunk in our bitmap
        offset = original_value_counter % NUM_BITS_IN_CHUNK;
        // To find which chunk offset belongs to
        chunk = (original_value_counter % BUFFER_SIZE_IN_PACKETS) / NUM_BITS_IN_CHUNK;
        return_value = InterlockedBitTestAndSet64((PLONGLONG)&g_receiver_state.packet_cache.reserve_cache_slot[chunk], offset);
        // We found a slot
        if (return_value == 0) {
            found_slot = TRUE;
        }
        // We did not find a slot so we move on to the next space on the cache
        else {
            // Once we reach max attempts, we drop the packet which is fine because sender side will send it again
            if (attempts == MAX_ATTEMPTS_RECIEVER) {
                return PACKET_CACHE_FAIL;
            }

           attempts++;
        }
        // we should increment regardless
        InterlockedIncrement((PLONG)&g_receiver_state.packet_cache.slot_counter_writer);
    }
    // Write packet into the circular buffer
    memcpy(&g_receiver_state.packet_cache.packet_space[chunk * NUM_BITS_IN_CHUNK + offset],
        Niko_Packet, sizeof(DATA_PACKET));
    // Update bitmap for reader side to indicate a packet can be read from this slot we reserved
    //g_receiver_state.packet_cache.is_cache_slot_written[chunk * NUM_BITS_IN_CHUNK + offset] = 1;
    InterlockedBitTestAndSet64((volatile PLONG64)&(g_receiver_state.packet_cache.is_cache_slot_written[chunk]), offset);
    SetEvent(g_receiver_state.packet_cache.packets_waiting_in_cache);
    return PACKET_CACHE_SUCCESSFUL;

}

BYTE read_from_cache(PDATA_PACKET Noah_Packet) {
    // Make sure packet exists/if Noah does a bad job
    ASSERT(Noah_Packet);
    boolean found_packet = FALSE;
    int attempts = 0;
    int return_value = 0;
    int chunk = 0;
    int offset = 0;
    int original_value_counter = 0;
    while (found_packet == FALSE) {
        // Check bit maps to see if there is any packets in the cache to read from - From the Blickster himself
        if (g_receiver_state.packet_cache.is_cache_slot_written[0] == 0 &&
            g_receiver_state.packet_cache.is_cache_slot_written[1] == 0) {
            return PACKET_FAILED_TO_READ;
        }
        // Take a "snapshot" / save original value of slot counter so that we can do consistent math even if other
        // threads increment slot_counter
        original_value_counter = g_receiver_state.packet_cache.slot_counter_reader;
        // To find the correct slot in any chunk in our bitmap
        offset = original_value_counter % NUM_BITS_IN_CHUNK;
        // To find which chunk offset belongs to
        chunk = (original_value_counter % BUFFER_SIZE_IN_PACKETS) / NUM_BITS_IN_CHUNK;
        return_value = InterlockedBitTestAndReset64((PLONGLONG)&g_receiver_state.packet_cache.is_cache_slot_written[chunk], offset);
        // We found a packet
        if (return_value == 1) {
            found_packet = TRUE;
        }
        else {

            // TODO: DONT think we need this
            attempts++;
            // if we checked the entire cache
            if (attempts == BUFFER_SIZE_IN_PACKETS) {
                return PACKET_FAILED_TO_READ;
            }
        }
        //regardless of if we succeed, we should increment this, before hand it was only in failure. //TODO
        InterlockedIncrement((PLONG)&g_receiver_state.packet_cache.slot_counter_reader);
    }
    memcpy(Noah_Packet,
        &g_receiver_state.packet_cache.packet_space[chunk * NUM_BITS_IN_CHUNK + offset]
        ,sizeof(DATA_PACKET));


    // Update bitmap for writer side to indicate this cache slot is available to be written into again //TODO confusing bitmap with bytemap
    return_value = InterlockedBitTestAndReset64((PLONGLONG)&g_receiver_state.packet_cache.reserve_cache_slot[chunk], offset);
    // this must have previously been claimed to then be released, so we should have a 1
    ASSERT(return_value == 1);
    return PACKET_SUCCESSFULLY_READ;
}