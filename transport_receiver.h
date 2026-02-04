#pragma once

#include "utils.h"
#include "transport_packets.h"

/**
 *
 *  Key data structures:
 *      - For all transmissions, there is a sparse array. The index in the
 *        sparse array is the transmission ID.
 *
 *      - Each entry in this sparse array will include a pointer to the
 *        data for that specific transmission. There is one pointer to the
 *        status bitmap for its packets and another pointer to the buffer of
 *        the data that has actually been received for that transmission.
 *
 */

// This defines the number of packets that are saved in the circular buffer.
// Cache_packet writes into it and main receiver thread pulls from it.
#define BUFFER_SIZE 128

typedef struct {
    PULONG64 status_bitmap;
    PVOID transmission_data;
} TRANSMISSION_INFO, *PTRANSMISSION_INFO;

typedef struct {
    PTRANSMISSION_INFO transmission_info_sparse_array;

    // This is the circular buffer that cache packet writes into
    // and the main thread reads from.
    DATA_PACKET packet_space[BUFFER_SIZE];
    UINT32 next_available_buffer_slot;              // TODO you can rename these or move them into their own struct.
    UINT32 buffer_slot_of_next_packet_to_process;

} RECEIVER_STATE, *PRECEIVER_STATE;

extern RECEIVER_STATE g_receiver_state;

/**
 * Creates data structures for a NEW transmission.
 * When a packet arrives with a new and unique transmission ID,
 * this function will initialize its status bitmap as well as its
 * sparse array of data.
 */
void init_received_transmission(void);

/**
 * Called by main receiver thread.
 * This adds the packet's data to its corresponding transmission info.
 * It will update the bitmap associated with the transmission and copy
 * the data from the packet's payload into the transmission's data buffer.
 */
void document_received_transmission(PDATA_PACKET pkt);

/**
 * Initializes data structures and launches threads for the receiver:
 *  - Reserves the sparse array for all transmission info entries.
 *  - Launches main receiver thread.
 */
void create_receiver(void);

/**
 * @brief Adds the given packet to a queue of packets to be processed.
 * @param pkt The packet to be added.
 * @retval 1 Packet is succesfully received
 * @retval 0 Packet is rejected.
 */
int cache_packet(PDATA_PACKET pkt);


// TODO ask Landy why we need a specific return type here
/**
 * @par Woken by cache packet when packets are available to be processsed.
 *          Sends ACKs and NACKs via comm packets.
 *          Updates sparse array data structures to track the status of each packet in a transmission.
 *          There are two data structures updated: there is a bitmap per transmission tracking the presence
 *          of each packet. And there is a sparse array of packet data into which the packets processed by
 *          this thread will be memcopied.
 * @param param
 * @return
 */
DWORD main_receiver_thread(LPVOID param);

