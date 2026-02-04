//
// Created by zachb on 1/30/2026.
//

#pragma once

#include "utils.h"

/**
 *  The way you structure your packet structs is entirely up to you.
 *  The network layer only asks for this structure:
 *
 *   ===============================================================================
 *   ||   UNIVERSAL PACKET HEADER    ||   DATA / COMM HEADER   ||     PAYLOAD     ||
 *   ===============================================================================
 *
 *  UNIVERSAL PACKET HEADER
 *  The universal header can contain any number of fields, but it MUST begin with the size of the
 *  universal header (ULONG64). After that, it must include the transmission ID and the packet type.
 *  Finally, the total size of the payload (in bytes) is included.
 *  As packet structures grow and change over time, the struct can expand to hold more fields
 *  and minimal edits will need to be made to the code.
 *
 *  DATA / COMM HEADER
 *  Following the universal header is a header specifically for either a data packet or a comm packet.
 *  This also begins with the number of bytes in the header (ULONG64). Additional fields can follow.
 *
 *  PAYLOAD
 *  Finally, after the data or comm header is the payload.
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  EXAMPLES
 *  The structs below are suggestions based on what we have discussed
 *  so far as a group. Modify as you see fit, as long as you still honor
 *  the expectations outlined above.
 *
 *  If you add additional fields to these structs later on, the network layer will still be able
 *  to support them. Just be sure to increase the size of the
 *
 *  Usage:
 *      Let's say you created a data packet:
 *
 *        DATA_PACKET d;
 *
 *      And you fully initialize it. To pass this to the network layer, you would say:
 *
 *         send_packet((PPACKET) &d);
 *
 *      This uses the shared PACKET struct (defined in utils.h).
 *
 *      Similarly, you can get a packet from the network like this:
 *
 *          DATA_PACKET d;
 *          receive_packet((PPACKET) &d, TIMEOUT_MS, ROLE_RECEIVER);
 **/

typedef struct data_packet {
    /* UNIVERSAL HEADER */
    ULONG64 bytes_in_header;                // Describes the size of the universal header (including this field).
                                            // Currently, this is always 16

    UINT32 transmission_id : 31;            // Indicates which transmission this packet belongs to.
    UINT32 must_be_zero : 1;                // When this bit is cleared, we interpret the packet as a data packet.
    UINT32 bytes_in_payload;                // Documents how many bytes in the payload are relevant.
                                            // This must be > 0 and < MAX_PAYLOAD_SIZE.

    /* DATA HEADER */
    ULONG64 bytes_in_data_fields;           // Describes the size of the data packet specific fields (including this field).
                                            // Currently, this is always 16.
    UINT32 index_in_transmission;           // Indicates the packet's position in the transmission (e.g. packet #3/5)
    UINT32 n_packets_in_transmission;       // Contains the total number of packets in this transmission.

    BYTE data[MAX_PAYLOAD_SIZE];            // Contains the data to be transmitted.
} DATA_PACKET, *PDATA_PACKET;


typedef struct comm_packet {
    /* UNIVERSAL HEADER */
    ULONG64 bytes_in_header;                // Describes the size of the universal header (including this field).
                                            // Currently, this is always 16

    UINT32 transmission_id : 31;            // Indicates which transmission we are acknowledging.
    UINT32 must_be_one : 1;                 // When this bit is set, we interpret the packet as a comm packet.
    UINT32 bytes_in_bitmap;                 // Documents the total size of the bitmap in bytes.
                                            // This must be > 0 and < MAX_PAYLOAD_SIZE.

    /* COMM HEADER */
    ULONG64 bytes_in_comm_fields;           // Describes the size of the data packet specific fields (including this field).
                                            // Currently, this is always 16.
    UINT32 first_packet_index;              // Indicates the packet number of the first packet in the bitmaps.
    UINT32 n_bits_to_read;                  // Indicates the number of relevant bits in the bitmap (corresponding
                                            // with the number of packets being ACKed or NACKed.

    /* PAYLOAD */
    BYTE bitmap[MAX_PAYLOAD_SIZE];          // Contains bitmap, where each 0 indicates a packet that WAS NOT
                                            // received and a 1 indicates a packet that WAS received.
} COMM_PACKET, *PCOMM_PACKET;