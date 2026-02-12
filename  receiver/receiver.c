//
// Created by nrper on 2/7/2026.
//

#include  "../transport_receiver.h"

RECEIVER_STATE g_receiver_state;

/**
 * Initializes data structures and launches threads for the receiver:
 *  - Reserves the sparse array for all transmission info entries.
 *  - Launches main receiver thread.
 */
void create_receiver(void) {

    // reserve memory for every possible id
    g_receiver_state.transmission_info_sparse_array = VirtualAlloc(NULL, sizeof(TRANSMISSION_INFO) * MAXULONG32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // start the main receiver thread
    g_receiver_state.receiver_thread = CreateThread(NULL, 0, main_receiver_thread, NULL, 0, NULL);

    // initialize the event that signals when packets are available
    g_receiver_state.packet_cache.packets_waiting_in_cache = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);

    // initialize the circular buffer
    g_receiver_state.packet_cache.buffer_slot_of_next_packet_to_process = 0;
    g_receiver_state.packet_cache.next_available_buffer_slot = 0;
}


void init_received_transmission(ULONG32 id, ULONG32 num_packets) {
    ULONG64 address_of_transmission_info = (ULONG64) &g_receiver_state.transmission_info_sparse_array[id];
    ULONG64 pageDataStartsOn = address_of_transmission_info & ~(PAGE_SIZE_IN_BYTES - 1);
    ULONG64 pageDataEndsOn = (address_of_transmission_info + sizeof(TRANSMISSION_INFO)) & ~(PAGE_SIZE_IN_BYTES - 1);


    VirtualAlloc( (LPVOID) pageDataStartsOn, pageDataEndsOn - pageDataStartsOn + PAGE_SIZE_IN_BYTES, MEM_COMMIT, PAGE_READWRITE);


    g_receiver_state.transmission_info_sparse_array[id].transmission_data = VirtualAlloc(NULL, num_packets * PACKET_PAYLOAD_SIZE_IN_BYTES,  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);


    ULONG64 numBitmaps = (num_packets + 63) / 64;


    // This just commits it straight up, as we get bigger file sizes, I will do the same reserve and commit strategy
    g_receiver_state.transmission_info_sparse_array[id].status_bitmap = VirtualAlloc(NULL, numBitmaps * sizeof(ULONG64),   MEM_COMMIT, PAGE_READWRITE);

    // if (num_packets % 64 != 0) {
    //     ULONG64 num_remaining_packets = num_packets % 64;
    //     // set the bits we do not need to 1
    //
    //     g_receiver_state.transmission_info_sparse_array[id].status_bitmap[numBitmaps - 1] = ~((1ULL << (num_remaining_packets + 1)) - 1);
    //
    // }

    g_receiver_state.transmission_info_sparse_array[id].num_packets_left = num_packets;
    g_receiver_state.transmission_info_sparse_array[id].transmission_complete_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);

}

/**
 * Called by main receiver thread.
 * This adds the packet's data to its corresponding transmission info.
 * It will update the bitmap associated with the transmission and copy
 * the data from the packet's payload into the transmission's data buffer.
 */
void document_received_transmission(PDATA_PACKET pkt) {
    TRANSMISSION_INFO *transmission_info = &g_receiver_state.transmission_info_sparse_array[pkt->transmission_id];
    ULONG64 packetNumber = pkt->index_in_transmission;


    // Set this right bit
    ULONG64 bitmapIndex = packetNumber / 64;
    LONG64 bitIndex = packetNumber % 64;

    ULONG64 output;
    output = _interlockedbittestandset64(&transmission_info->status_bitmap[bitmapIndex], bitIndex);
    if(output == 0) {
        return;
    }

    ULONG64 addressToWrite = (ULONG64) transmission_info->transmission_data + packetNumber * 1024;
    ULONG64 pageDataStartsOn = addressToWrite & ~(PAGE_SIZE_IN_BYTES - 1);
    ULONG64 pageDataEndsOn = (addressToWrite + PACKET_PAYLOAD_SIZE_IN_BYTES) & ~(PAGE_SIZE_IN_BYTES - 1);

    VirtualAlloc( (LPVOID) pageDataStartsOn, pageDataEndsOn - pageDataStartsOn + PAGE_SIZE_IN_BYTES, MEM_COMMIT, PAGE_READWRITE);



    memcpy((PVOID) addressToWrite, &pkt->data, PACKET_PAYLOAD_SIZE_IN_BYTES);




    ULONG64 packetsLeft = InterlockedDecrement64(&transmission_info->num_packets_left);

    ASSERT(packetsLeft != MAXULONG64)
    if (packetsLeft == 0) {
        SetEvent(transmission_info->transmission_complete_event);
    }
}