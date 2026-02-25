//
// Created by porte on 2/25/2026.
//

#include "../transport.h"
#include "../transport_sender.h"


SENDER_STATE g_sender_state;


VOID create_sender(VOID)
{
    g_sender_state.transmissions_in_progress =
        VirtualAlloc(NULL,
        MAXULONG32 * sizeof(SENDER_TRANSMISSION_INFO),
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);


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
    ULONG64 timeout_ms = 100;

    COMM_PACKET packet; // Will need to change this to an array of packet locations and receive them there ?

    while (TRUE)
    {
        int packet_received_status = receive_packet((PPACKET) &packet, timeout_ms, ROLE_SENDER);
        if (packet_received_status == NO_PACKET_AVAILABLE)
        {
            break;
        }

        // Immediately write out the comms we received to our transmission bitmaps for the minions.

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

VOID find_work(VOID)
{

}