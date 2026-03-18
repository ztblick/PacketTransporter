//
// Created by nrper on 2/7/2026.
//

#include  "../transport_receiver.h"

RECEIVER_STATE g_receiver_state;

// Initializes the fields inside PACKET_CACHE cache
void initialize_cache(void){
    // initialize the event that signals when packets are available
    g_receiver_state.packet_cache.packets_waiting_in_cache =
        CreateEvent(NULL, AUTO_RESET, FALSE, NULL);

    // initialize the circular buffer
    g_receiver_state.packet_cache.slot_counter_reader = 0;
    g_receiver_state.packet_cache.slot_counter_writer = 0;

    // Initialize the buffer that we will write into
    memset(g_receiver_state.packet_cache.packet_space, 0, BUFFER_SIZE_IN_PACKETS);

    // Initialize Bitmaps
    memset(g_receiver_state.packet_cache.reserve_cache_slot, 0,
        sizeof(g_receiver_state.packet_cache.reserve_cache_slot));
    memset(g_receiver_state.packet_cache.is_cache_slot_written, 0,
        sizeof(g_receiver_state.packet_cache.is_cache_slot_written));
}

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

    initialize_cache();
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

boolean check_transmission(UINT32 transmission_id) {

    __try {
        // check if it is initialized
        if (g_receiver_state.transmission_info_sparse_array[transmission_id].num_packets_left == 0)
            return TRUE;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }

    return FALSE;
}


int reciever_handler(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms) {
    // TODO: Student implementation
    // - Receive packets via receive_packet() or try_receive_packet()
    // - Reassemble packets into complete transmissions
    // - When complete, fill in out_id, dest, out_length and return 1
    int result;
    //Checks to see if the transmission has been initialized

    ULONG64 time = time_now_ms();
    ULONG64 deadline = time + timeout_ms;

    while (time_now_ms() < deadline) {

        if (check_transmission(transmission_id)) {

            PTRANSMISSION_INFO info = &g_receiver_state.transmission_info_sparse_array[transmission_id];
            size_t file_size = info->file_size_in_bytes;

            // Write all data from global struct into this transmission's memory (dest)
            memcpy(dest, info->transmission_data, file_size);

            // Update the transmission's size (out_length)
            *out_length = file_size;

            // Finish the transmission and return
            return TRANSMISSION_RECEIVED;
        }

        //Calls receive packet at the dest and saves the result to check if transmission was successful
        //Here I create a local data packet to memcopy space for the transmission
        DATA_PACKET local_pkt;
        result = receive_packet((PPACKET) &local_pkt, 10, ROLE_RECEIVER);

        if (result == NO_PACKET_AVAILABLE) {
            continue;
        }
        write_to_cache(&local_pkt);
    }

    //If we get to this point, we know runtime exceeded the deadline threshold
    return NO_TRANSMISSION_AVAILABLE;
}