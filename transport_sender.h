#pragma once

/**
 * Key design choice:
 *  - Application layer calls to send_transmission() will NOT return
 *    until all data in the transmission has been ACK'd by the receiver.
 *    (At least, for now -- v1).
 *
 * Initializes data structures and launches threads for the sender:
 *  - TKTKTK
 */
VOID create_sender(VOID);

/**
 * This is called from send_transmission, who will specify the
 * offset into the transmission as well as the amount of data to
 * split into packets and send to the network layer.
 *
 * This function creates the individual packets. For security purposes,
 * the memcpy into the packet is protected with a try-except.
 *
 * @param transmission_data The offset into the transmission where
 * we begin packetizing.
 * @param bytes_to_packetize The number of bytes to packetize.
 */
VOID packetize_contiguous(PVOID transmission_data, ULONG64 bytes_to_packetize);


/**
 * Clears the packet buffer, sending all packets to the network.
 *  If the network rejects a packet, this function will again attempt
 *  to send the packet.
 *
 *  This function needs to listen for the network shutdown event, otherwise
 *  it will never end.
 */
VOID send_packet_batch(ULONG64 number_of_packets_to_send);


DWORD sender_listener(LPVOID param);