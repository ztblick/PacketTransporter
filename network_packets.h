//
// Created by zachb on 1/30/2026.
//

#pragma once

#include "utils.h"

/**
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
 **/

/**
 *  These packet headers support conversion from a universal header to a packet-specific
 *  header. This conversion also supports access to the data. For example:
 *
 *  Transport layer calls: send_packet(&pkt, ROLE_SENDER);
 *
 *  Network layer can access the packet as a universal header first:
 *  PPACKET u_header = pkt;
 *  PDC_HEADER d_pkt;
 *
 *  Then, we can use the offset to find the beginning of the data header:
 *  if (u_header.packet_type == DATA_PACKET) {
 *      d_pkt = (PDC_HEADER) ((ULONGPTR) u_header + u_header.total_bytes_in_packet_header);
 *
 *  From here, we can verify the total size of the packet. Then we can accept or reject
 *  the packet.
 */
typedef struct data_comm_packet_header {
    ULONG64 total_bytes_in_dc_header;   // Describes the size of THIS header, which determines
                                        // where the payload will begin.
    // No other fields are relevant for the network layer.
} DC_HEADER, *PDC_HEADER;