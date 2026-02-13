#pragma once

typedef struct {

    // Bitmap (pointer) -- each bit indicates if a "chunk" of the
    // transmission has been assigned to a worker

    // Bitmap (pointer) -- each bit indicates if the "chunk" has
    // been fully sent and ACKed

    // Pointer to the transmission's data (given from send_transmission)

} SENDER_TRANSMISSION_INFO, *PSENDER_TRANSMISSION_INFO;

typedef struct {

    // Transmission ID

    // Pointer to its offset in the transmission data

    // Size of the chunk that is being packetized

    // Bitmap for packet status (ACKed or not) -- but needs to be accessed by sender_listener somehow...

} WORKER_THREAD_INFO, *PWWORKER_THREAD_INFO;

typedef struct {

    // Queue of transmission IDs to indicate which
    // transmission should be worked on next

    // Sparse array (index = transmission ID) of transmission info structs

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
DWORD sender_worker(LPVOID param);

/**
 * @brief Called by the sender worker thread to determine its next job.
 * This will give the thread a chunk of a transmission to send & check,
 * or it will put it to sleep if no work is available.
 */
VOID find_work(VOID);