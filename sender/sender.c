//
// Created by porte on 2/25/2026.
//

#include "../transport.h"
#include "../transport_sender.h"


SENDER_STATE g_sender_state;
TRANSMISSION_CACHE g_transmission_cache;


VOID create_sender(VOID)
{
    g_sender_state.transmissions_in_progress =
        VirtualAlloc(NULL,
        MAXULONG32 * sizeof(SENDER_TRANSMISSION_INFO),
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);


    g_transmission_cache.work_array = (PUINT32)VirtualAlloc(NULL,
        sizeof(UINT32) * WORK_ARRAY_SIZE,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);

    g_transmission_cache.next_chunk_index = 0;

    // Create sender listener thread.
    CreateThread(NULL, 0, sender_listener, NULL, 0, NULL);


    // Create our minion threads.
    for (int i = 0; i < SENDER_MINION_COUNT; i++) {
        CreateThread(NULL, 0, sender_minion, NULL, 0, NULL);
    }
}

VOID packetize_contiguous(PVOID transmission_data, ULONG64 bytes_to_packetize, SENDER_MINION_INFO minion_info)
{
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

        packet.index_in_transmission = i / MAX_PAYLOAD_SIZE;
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


    return 0;
}

PVOID find_work(VOID)
{
    g_sender_state.transmissions_in_progress->packet_status_bitmap;
}

UINT32 get_next_transmissionID(VOID) {
    while (&g_transmission_cache.work_array[g_transmission_cache.next_chunk_index] == NULL) {
        g_transmission_cache.next_chunk_index++;
    }
    UINT32 transmission_id = g_transmission_cache.work_array[g_transmission_cache.next_chunk_index];
    if (g_sender_state.transmissions_in_progress[transmission_id].number_of_packets_in_transmission != 1) {

    }
}