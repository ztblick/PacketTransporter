#pragma once

#include "network.h"
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
#define BUFFER_SIZE_IN_PACKETS 128
#define NUM_BITS_IN_CHUNK 64
#define MAX_ATTEMPTS 16

typedef struct {

    volatile PULONG64 status_bitmap;
    PVOID transmission_data;
    volatile ULONG64 num_packets_left;
    HANDLE transmission_complete_event;
    volatile size_t file_size_in_bytes;
} TRANSMISSION_INFO, *PTRANSMISSION_INFO;

typedef struct {

    // This is the circular buffer that cache packet writes into
    // and the main thread reads from.
    DATA_PACKET packet_space[BUFFER_SIZE_IN_PACKETS];
    // Writer index
    volatile UINT32 slot_counter_writer;
    // Reader index
    volatile UINT32 slot_counter_reader;
    // TODO: Initialize these bitmaps
    // Bitmap which indicate that a thread has taken a slot on the cache and is about to write into it
    ULONG64 reserve_cache_slot[2];
    // Bitmap which indicates which cache slots have been finished writing into
    // We need this bitmap in order to handle a case where we reserve a cache slot, but we haven't
    // finished writing into it. A case where we could try to read before we write into the cache slot
    ULONG64 is_cache_slot_written[2];
    // This event is used to wake the main receiver thread
    // when packets are added to the cache.
    HANDLE packets_waiting_in_cache;

} PACKET_CACHE, *PPACKET_CACHE;

typedef struct {
    // This sparse array stores the transmission information for transmission ID #N at index N in the array.
    PTRANSMISSION_INFO transmission_info_sparse_array;

    // this is the thread that processes packets in the cache
    HANDLE receiver_thread;

    PACKET_CACHE packet_cache;

} RECEIVER_STATE, *PRECEIVER_STATE;

extern RECEIVER_STATE g_receiver_state;

/**
 * Creates data structures for a NEW transmission.
 * When a packet arrives with a new and unique transmission ID,
 * this function will initialize its status bitmap as well as its
 * sparse array of data.
 *
 * @param id The unique transmission ID for this transmission.
 * @param num_packets The number of packets that will be received for this transmission.
 *
 */
void init_received_transmission(ULONG32 id, ULONG32 num_packets);

/**
 * Called by main receiver thread.
 * This adds the packet's data to its corresponding transmission info.
 * It will update the bitmap associated with the transmission and copy
 * the data from the packet's payload into the transmission's data buffer.
 * @pre assumed the transmission info is initialized
 */
void document_received_transmission(PDATA_PACKET pkt);

/**
 * Initializes data structures and launches threads for the receiver:
 *  - Reserves the sparse array for all transmission info entries.
 *  - Launches main receiver thread.
 */
void create_receiver(void);

#define PACKET_CACHE_SUCCESSFUL 1
#define PACKET_CACHE_FAIL       0
/**
 * @brief Adds the given packet to a queue of packets to be processed.
 * @param pkt The packet to be added.
 * @retval 1 Packet is successfully received
 * @retval 0 Packet is rejected -- this can happen when the buffer is full.
 */
BYTE write_to_cache(PDATA_PACKET pkt);

/**
 * @par Woken by cache packet when packets are available to be processed.
 *      Sends ACKs and NACKs via comm packets.
 *      Updates sparse array data structures to track the status of each packet in a transmission.
 *      There are two data structures updated: there is a bitmap per transmission tracking the presence
 *      of each packet. And there is a sparse array of packet data into which the packets processed by
 *      this thread will be memcpy'd. For security purposes, we guard the memcpy with a try-except.
 * @param param
 * @return
 */
DWORD main_receiver_thread(LPVOID param);

/**
 * @brief Adds the given packet to a queue of packets to be processed.
 * @param pkt The packet to be added.
 * @retval 1 Packet from cache was succesfully read
 * @retval 0 Packet is rejected -- this can happen when the buffer is full.
 */
#define PACKET_SUCCESSFULLY_READ 1
#define PACKET_FAILED_TO_READ 0
BYTE read_from_cache(PDATA_PACKET Noah_Packet);

#define TRANSMISSION_RECEIVED       0
#define NO_TRANSMISSION_AVAILABLE   1
int reciever_handler(UINT32 transmission_id, PVOID dest, PSIZE_T out_length, ULONG64 timeout_ms);

