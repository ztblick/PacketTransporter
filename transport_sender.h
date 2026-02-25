#pragma once

/**
 * When we split work across our sender minions (worker threads) we will need to know how many
 * packets are assigned to a minion. This is the maximum number of contiguous packets
 * we will assign to a minion at any time.
 */
#define MAX_CHUNK_SIZE_IN_PACKETS   4
#define SENDER_MINION_COUNT         2


typedef struct {

    /**
     * Bitmap (pointer): there is one bit per packet here. All are initially 0.
     * When a packet is ACK'd, its bit is set. Only the sender-listener sets these bits.
     *
     * E.G. Let's say no packets are ACK'd:                         000
     *      Then packet 1 is ACK'd. Sender listener sets its bit:   010
     *
     * TODO Ask LANDY about concerns about word tearing here. Do we need to do anything with read/write no fence?
     **/
    PULONG64 packet_status_bitmap;

    /**
     * This field will be atomically incremented. Each sender minion will do an interlocked increment on this
     * field to claim the next chunk of packets.
     */
    volatile ULONG64 next_chunk_index;

    // Initialized to describe the number of packets needed to send all of the transmission's data.
    ULONG64 number_of_packets_in_transmission;

    // Pointer to the transmission's data (given from send_transmission)
    PBYTE data;

} SENDER_TRANSMISSION_INFO, *PSENDER_TRANSMISSION_INFO;

typedef struct {

    // Transmission ID
    ULONG64 transmission_id;

    // Pointer to its offset in the transmission data
    PBYTE data_to_send;

    // Size of the chunk that is being packetized
    ULONG64 bytes_to_send;

} SENDER_MINION_INFO, *PSENDER_MINION_INFO;

/**
 * This data structure keeps track of the transmissions in the order in which they are received.
 * It facilitates the minions as they seek out the next chunk of work.
 */
typedef struct {
    // TODO implement an array and an index that will allow us to easily move from one transmission to the next

    // TODO think about adding a new transmission when it is received

    // TODO think about what happens when a transmission's final chunk is assigned
} TRANSMISSION_CACHE;

typedef struct {

    // Queue of transmission IDs to indicate which
    // transmission should be worked on next
    TRANSMISSION_CACHE transmissions_queue;

    // Sparse array (index = transmission ID) of transmission info structs
    PSENDER_TRANSMISSION_INFO transmissions_in_progress;

} SENDER_STATE, *PSENDER_STATE;


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
 *  @brief Clears the packet buffer, sending all packets to the network.
 *  If the network rejects a packet, this function will again attempt
 *  to send the packet.
 *
 *  This function needs to listen for the network shutdown event, otherwise
 *  it will never end.
 */
VOID send_packet_batch(ULONG64 number_of_packets_to_send);

/**
 *
 * @brief The sender listener thread calls receive_packet to check for
 * incoming Comm Packets. When they arrive, this thread will
 *
 * There will be one sender listener running on each "machine".
 *
 *
 * @param param
 * @return
 */
DWORD sender_listener(LPVOID param);


/**
 * @brief Created when the transport layer is initialized. These worker threads
 * call find_work() to be assigned a chunk of a transmission to send. They will
 * packetize that chunk, then push them to the network via send_packet(). They
 * will not move on to a new chunk until all packets in that chunk have been ACKed.
 *
 * Potential future optimization: worker thread has two chunks to manage -- one to
 * packetize and send, one to check and ACK.
 *
 * @param param
 * @return
 */
DWORD sender_minion(LPVOID param);

/**
 * @brief Called by the sender worker thread to determine its next job.
 * This will give the thread a chunk of a transmission to send & check,
 * or it will put it to sleep if no work is available.
 */
VOID find_work(VOID);