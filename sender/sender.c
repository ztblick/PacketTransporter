//
// Created by porte on 2/25/2026.
//

#include "../transport.h"
#include "../transport_sender.h"


SENDER_STATE g_sender_state;
CRITICAL_SECTION g_work_array_lock;


VOID create_sender(VOID)
{
    g_sender_state.transmissions_in_progress =
        VirtualAlloc(NULL,
        MAXULONG32 * sizeof(SENDER_TRANSMISSION_INFO),
        MEM_RESERVE,
        PAGE_READWRITE);
    if (g_sender_state.transmissions_in_progress == NULL) {
        DebugBreak();
    }



    g_sender_state.transmissions_queue.work_array = (PUINT32)VirtualAlloc(NULL,
        sizeof(UINT32) * WORK_ARRAY_SIZE,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);
    if (g_sender_state.transmissions_queue.work_array == NULL) {
        DebugBreak();
    }

    // Filling work array with default values
    memset(g_sender_state.transmissions_queue.work_array, EMPTY_WORK_ARRAY_ID, sizeof(UINT32) * WORK_ARRAY_SIZE);

    g_sender_state.transmissions_queue.next_read_index = 0;
    g_sender_state.transmissions_queue.next_write_index = 0;

    // Create sender listener thread.
    CreateThread(NULL, 0, sender_listener, NULL, 0, NULL);

    InitializeCriticalSection(&g_work_array_lock);

    // Create our minion threads.
    for (int i = 0; i < SENDER_MINION_COUNT; i++) {
        CreateThread(NULL, 0, sender_minion, NULL, 0, NULL);
    }
}



VOID packetize_contiguous(PVOID transmission_data, ULONG64 bytes_to_packetize, SENDER_MINION_INFO minion_info)
{

    ULONG64 numPackets;
    DATA_PACKET packet;
    ULONG64 bytes_left_to_packetize = bytes_to_packetize;
    // right now we are just assuming that we want every packet to be as full as possible.
    numPackets = bytes_to_packetize / MAX_PAYLOAD_SIZE;
    if (bytes_to_packetize % MAX_PAYLOAD_SIZE != 0) {
        numPackets++;
    }

    UINT32 starting_packet_numer = minion_info.chunk_index * MAX_CHUNK_SIZE_IN_PACKETS;

    for (int i = 0; i < numPackets; i++) {
        // I feel like there is an easier way of organizing the fields, but it would require a lot of blick work.
        packet.index_in_transmission = starting_packet_numer + i;
        packet.transmission_id = minion_info.transmission_id;
        packet.n_packets_in_transmission = minion_info.n_packets_in_transmission;
        packet.must_be_zero = 0;
        packet.bytes_in_header = 16;
        packet.bytes_in_data_fields = 16;
        packet.bytes_in_payload = min(bytes_left_to_packetize, MAX_PAYLOAD_SIZE);

        __try {
            memcpy(packet.data,(PVOID) ((ULONG64) transmission_data + i * MAX_PAYLOAD_SIZE), packet.bytes_in_payload);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("Failed to copy data to packet, likely a hack attempt\n");
            DebugBreak();
            exit(1);
        }

        bytes_left_to_packetize -= packet.bytes_in_payload;

        if (send_packet((PPACKET) &packet, ROLE_SENDER) == PACKET_REJECTED) {
            printf("Failed to send packet\n");
            DebugBreak();
        }
# if SUPERFLUOUS_PRINTS
    printf("Sending packet with id %llu and index %llu\n", packet.transmission_id,packet.index_in_transmission);
#endif
    }

#if 0
    ULONG64 bytes_left_to_packetize = bytes_to_packetize;
    for (int i = 0; i < bytes_to_packetize; i+= MAX_PAYLOAD_SIZE)
    {
        DATA_PACKET packet;

        if (bytes_left_to_packetize > MAX_PAYLOAD_SIZE)
        {
            bytes_left_to_packetize -= MAX_PAYLOAD_SIZE;
            packet.bytes_in_payload = MAX_PAYLOAD_SIZE;
        }
        else
        {
            packet.bytes_in_payload = bytes_left_to_packetize;
        }

        __try {
            memcpy(packet.data, transmission_data + i, packet.bytes_in_payload);
        }
        __except (EXCEPTION_EXECUTE_HANDLER){
            // Not sure what to put here?
        }



        // Hardcoded this to 16 since that's what we decided these will be but don't like having magic numbers here.
        packet.bytes_in_header = 16;
        packet.bytes_in_data_fields = 16;

        packet.index_in_transmission = minion_info.chunk_index * MAX_CHUNK_SIZE_IN_PACKETS + (i / MAX_PAYLOAD_SIZE);
        packet.must_be_zero = 0;
        packet.n_packets_in_transmission = minion_info.n_packets_in_transmission;
        packet.transmission_id = minion_info.transmission_id;

        // Do I want to do this here??
        // g_sender_state.transmissions_in_progress[minion_info.transmission_id]
        // .number_of_packets_in_transmission++;

        // Not using send packet batch for now.
        if (send_packet((PPACKET) &packet, ROLE_SENDER) == PACKET_REJECTED)
        {
            DebugBreak();
        }

    }

#endif

}

VOID send_packet_batch(ULONG64 number_of_packets_to_send)
{

}


DWORD sender_listener(LPVOID param)
{
    ULONG64 timeout_ms = 100;

    COMM_PACKET packet; // Will need to change this to an array of packet locations and receive them there ?

    while (TRUE)
    {
        int packet_received_status = receive_packet((PPACKET) &packet, timeout_ms, ROLE_SENDER);
        if (packet_received_status == NO_PACKET_AVAILABLE)
        {
            continue;
        }

        UINT32 transmission_id = packet.transmission_id;

        // Immediately write out the comms we received to our transmission bitmaps for the minions.
        PSENDER_TRANSMISSION_INFO transmission_info = &g_sender_state.transmissions_in_progress[transmission_id];


        for (int i = 0; i < packet.n_bits_to_read; i++)
        {
            BYTE current_byte = packet.bitmap[i / 8];

            // Had to look up this bitwise operator stuff but I think it's right.
            int is_bit_set = current_byte & 1 << (i % 8);

            // Weird thing where I have to divide by 64 instead of 8 because the packet status bitmap is 64 bits
            // as opposed to the packet bitmap's 8 bit data type.
            if (is_bit_set)
            {
                UINT32 packet_index = packet.first_packet_index + i;
                transmission_info->packet_status_bitmap[packet_index / 64] |= 1ULL << (packet_index % 64);
            }

        }
#if SUPERFLUOUS_PRINTS
    printf("Received ack packet with id %llu and index %llu\n, here is the first bitmap %llu \n", transmission_id, packet.first_packet_index, packet.bitmap[0]);
#endif

    }

    /*
    *Wait for single object (packets sent)
    Receive packet (timeout infinite)
    then loop try recieve packet until no more
    then go back to receive packet
    */

    return 0;
}

DWORD sender_minion(LPVOID param)
{
    // Init our briefcase to just 0 values
    SENDER_MINION_INFO briefcase = {0};
    PSENDER_MINION_INFO p_briefcase = &briefcase;

    while (TRUE)
    {
        // Find our next work and update the briefcase with its info.
        find_work(p_briefcase);

        // Check that we were able to find any work (if not, wait and retry).
        if (p_briefcase->transmission_id == EMPTY_WORK_ARRAY_ID) {
            Sleep(10);
            continue;
        }

        // Packetize and send the data from the briefcase.
        packetize_contiguous(p_briefcase->data_to_send, p_briefcase->bytes_to_send, briefcase);

        // Now we set up the ack'ing
        PULONG64 bitmap = g_sender_state.transmissions_in_progress[p_briefcase->transmission_id].packet_status_bitmap;
        ULONG64 first_packet = p_briefcase->chunk_index * MAX_CHUNK_SIZE_IN_PACKETS;
        ULONG64 num_packets = (p_briefcase->bytes_to_send + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;

        // While loop until everything is successfully ack'd.
        boolean all_acked = FALSE;
        while (!all_acked)
        {
            // Wait for the network latency before checking. Slight magic number but wanted
            // to add a bit of time for any overhead.
            Sleep(LATENCY_MS * 1.2);
            all_acked = TRUE;

            // Go through the number of packets this minion is supposed to be working on.
            for (int i = 0; i < num_packets; i++)
            {
                ULONG64 packet_num = first_packet + i;

                // Check that the current packet is ack'd, and if not, set all_acked to false.
                if (!(bitmap[packet_num / 64] & 1ULL << (packet_num % 64)))
                {
                    all_acked = FALSE;
                }

            }
        }

        PSENDER_TRANSMISSION_INFO info = &g_sender_state.transmissions_in_progress[p_briefcase->transmission_id];

        // Check ALL of our packets in the transmission to see if the entire transmission is done.
        boolean transmission_done = TRUE;
        for (int i = 0; i < info->number_of_packets_in_transmission; i++)
        {
            if (!(bitmap[i / 64] & 1ULL << (i % 64)))
            {
                // If the entire transmission isn't done, just break.
                transmission_done = FALSE;
                break;
            }
        }

        // Call our sending complete event if the entire transmission is finished.
        if (transmission_done)
        {
            SetEvent(info->sending_complete_event);
        }

    }


    return 0;
}

VOID find_work(PSENDER_MINION_INFO briefcase)
{
    // Get our work array lock before we modify the work array in get next transmission ID
    EnterCriticalSection(&g_work_array_lock);
    briefcase->transmission_id = get_next_transmission_id();
    LeaveCriticalSection(&g_work_array_lock);

    // Return if we can't find any transmissions to work on next.
    if (briefcase->transmission_id == EMPTY_WORK_ARRAY_ID) {
        return;
    }

    PSENDER_TRANSMISSION_INFO info = &g_sender_state.transmissions_in_progress[briefcase->transmission_id];

    // Interlocked increment our chunk index so it is safe across the multiple threads.
    ULONG64 chunk_index = InterlockedIncrement64((volatile LONG64*) &info->next_chunk_index) - 1;

    // Check if this chunk is past the end of the transmission
    ULONG64 first_packet_of_chunk = chunk_index * MAX_CHUNK_SIZE_IN_PACKETS;
    if (first_packet_of_chunk >= info->number_of_packets_in_transmission)
    {
        briefcase->transmission_id = EMPTY_WORK_ARRAY_ID;
        
        return;
    }

    // Fill the briefcase.
    briefcase->chunk_index = chunk_index;
    briefcase->n_packets_in_transmission = info->number_of_packets_in_transmission;
    briefcase->data_to_send = info->data + chunk_index * MAX_CHUNK_SIZE_IN_PACKETS * MAX_PAYLOAD_SIZE;

    // Calc the number of bytes we need to send (make sure we get the right amount if
    // at the last packet which might not be totally full)
    ULONG64 byte_offset = chunk_index * MAX_CHUNK_SIZE_IN_PACKETS * MAX_PAYLOAD_SIZE;
    briefcase->bytes_to_send = min(info->total_bytes - byte_offset, MAX_CHUNK_SIZE_IN_PACKETS * MAX_PAYLOAD_SIZE);


}

UINT32 get_next_transmission_id(VOID) {
    // Circular buffer so doesn't go out of bounds
    UINT32 slots_checked = 0;
    while (g_sender_state.transmissions_queue.work_array[g_sender_state.transmissions_queue.
        next_read_index % WORK_ARRAY_SIZE] == EMPTY_WORK_ARRAY_ID)
    {
        // Increase the read index (where we are up to reading the transmission IDs in transmission queue)
        g_sender_state.transmissions_queue.next_read_index++;
        slots_checked++;
        if (slots_checked >= WORK_ARRAY_SIZE) 
        {
            return EMPTY_WORK_ARRAY_ID;
        }
    }

    UINT32 return_ID = g_sender_state.transmissions_queue.work_array[g_sender_state.transmissions_queue.
        next_read_index % WORK_ARRAY_SIZE];

    // Clear the ID I returned so it can be reused
    g_sender_state.transmissions_queue.work_array[g_sender_state.transmissions_queue.
        next_read_index % WORK_ARRAY_SIZE] = EMPTY_WORK_ARRAY_ID;



    // // TODO
    // PSENDER_TRANSMISSION_INFO info = &g_sender_state.transmissions_in_progress[return_ID];
    //
    // // Interlocked increment our chunk index so it is safe across the multiple threads.
    // ULONG64 chunk_index = InterlockedIncrement64((volatile LONG64*) &info->next_chunk_index) - 1;
    //
    // // Check if this chunk is past the end of the transmission
    // // if we are at the last packet, we don't need to add it back to the array, I am using the previous logic, there is probably a better way to do this.'
    // ULONG64 first_packet_of_chunk = chunk_index * MAX_CHUNK_SIZE_IN_PACKETS;
    // if (first_packet_of_chunk >= info->number_of_packets_in_transmission) {
    //
    // } else {
        g_sender_state.transmissions_queue.work_array[g_sender_state.transmissions_queue.next_write_index % WORK_ARRAY_SIZE] = return_ID;
        g_sender_state.transmissions_queue.next_write_index++;

    //}
    // there is a slowdown where we are not checking if it is the last packet in the transmission

    return return_ID;
}