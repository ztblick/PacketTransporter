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
    
    return TRANSMISSION_ACCEPTED;
}


int receive_transmission(uint32_t* out_id, void* dest, size_t* out_length, int timeout_ms) {

    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Reassemble packets into complete transmissions

    // - When complete, fill in out_id, dest, out_length and return 1

    return NO_TRANSMISSION_AVAILABLE;
}
BYTE write_to_cache(PDATA_PACKET Niko_Packet) {
    // Make sure packet exists/if Niko does a bad job
    ASSERT(Niko_Packet);
    // Attempt to reserve slot in cache to write into
    boolean found_slot = FALSE;
    int attempts = 0;
    int return_value = 0;
    int chunk = 0;
    int offset = 0;
    int slot = 0;
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
            if (attempts == MAX_ATTEMPTS) {
                return PACKET_CACHE_FAIL;
            }
           InterlockedIncrement((PLONG)&g_receiver_state.packet_cache.slot_counter_writer);
           attempts++;
        }
    }
    // Write packet into the circular buffer
    memcpy(&g_receiver_state.packet_cache.packet_space[chunk * NUM_BITS_IN_CHUNK + offset],
        Niko_Packet, PACKET_PAYLOAD_SIZE_IN_BYTES);
    // Update bitmap for reader side to indicate a packet can be read from this slot we reserved
    g_receiver_state.packet_cache.is_cache_slot_written[chunk * NUM_BITS_IN_CHUNK + offset] = 1;
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
        return_value = InterlockedBitTestAndReset64(
            (PLONGLONG)&g_receiver_state.packet_cache.is_cache_slot_written[chunk], offset);
        // We found a packet
        if (return_value == 1) {
            found_packet = TRUE;
        }
        else {
            InterlockedIncrement((PLONG)&g_receiver_state.packet_cache.slot_counter_reader);
            // TODO: DONT think we need this
            attempts++;
            // if we checked the entire cache
            if (attempts == BUFFER_SIZE_IN_PACKETS) {
                return PACKET_FAILED_TO_READ;
            }
        }
    }
    memcpy(Noah_Packet,
        &g_receiver_state.packet_cache.packet_space[chunk * NUM_BITS_IN_CHUNK + offset]
        ,PACKET_PAYLOAD_SIZE_IN_BYTES);
    // Update bitmap for writer side to indicate this cache slot is available to be written into again
    g_receiver_state.packet_cache.reserve_cache_slot[chunk * NUM_BITS_IN_CHUNK + offset] = 0;
    return PACKET_SUCCESSFULLY_READ;
}