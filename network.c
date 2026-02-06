/*
 * network.c
 * 
 * Network Layer Implementation
 */

#include "network.h"
#include "network_packets.h"

// These are our statuses for the metadata slots
enum {EMPTY, WRITING, READY, READING};

/*
 * Buffer entry objects - include packet and available time
 */
typedef struct buffer_packet_metadata {
    PVOID location_in_buffer;
    ULONG64 bytes_in_buffer : 62;
    ULONG64 status : 2;
    ULONG64 time_available;
} BUFFER_PACKET_METADATA, *PBUFFER_PACKET_METADATA;

/*
 * Network buffer: a contiguous region of memory into which packets are copied.
 * This structure is used to simulate the two network cards (NICs) as well as the network itself.
 * All packets are given a timestamp of current_time + latency as they enter the network.
 * This is used to determine the "arrival" time of the packet. This arrival time
 * determines when a packet is removed from the network buffer and moved to the inbound NIC
 * buffer.
 *
 * The implementation is a circular buffer. Packets can be of varying sizes. This is why
 * the metadata slots are used to facilitate access to the buffer data. When requesting
 * a slot for a packet, first a metadata slot is reserved. Then, if there is sufficient
 * space available in the buffer, a portion of the buffer will be reserved for the packet.
 * Its size and byte offset in the buffer are saved to the metadata.
 *
 * Atomic pointers to the buffer's read and write locations are maintained to facilitate access
 * and prevent race conditions. The same is true of the metadata slots.
 */
typedef struct packet_buffer {

    // This contains the metadata for all packets as they are added to the buffer.
    PBUFFER_PACKET_METADATA metadata_slots;
    ULONG64 num_metadata_slots;

    // This contains the actual data for the packets
    PBYTE buffer_data;
    ULONG64 buffer_size_in_bytes;

    /**
     * These values are used to atomically track the locations in the buffers
     * where threads can write into and read from. These are the variables most
     * likely to experience contention, so we put them on separate cache lines to
     * prevent false sharing.
     **/
    __declspec(align(64)) volatile ULONG64 metadata_write_slot;
    __declspec(align(64)) volatile ULONG64 metadata_read_slot;

    // When this buffer's event is set, a thread waiting for
    // a packet is woken. This is a manual reset event, as
    // threads should continue to consume packets until there are
    // NONE left.
    HANDLE packets_waiting_in_buffer;
} NETWORK_PACKET_BUFFER, *PNETWORK_PACKET_BUFFER;

/*
 *  Initialize a network buffer object.
 */
void initialize_network_packet_buffer(PNETWORK_PACKET_BUFFER buffer,
                                        ULONG64 metadata_slot_count,
                                        ULONG64 buffer_size_in_bytes) {

    // Create the data buffer and the metadata buffer, and initialize their sizes.
    buffer->buffer_data = zero_malloc(
        buffer_size_in_bytes
        );
    buffer->buffer_size_in_bytes = buffer_size_in_bytes;
    buffer->metadata_slots = zero_malloc(
        metadata_slot_count * sizeof(BUFFER_PACKET_METADATA)
        );
    buffer->num_metadata_slots = metadata_slot_count;

    // Set the atomic tracking variables to zero
    buffer->metadata_write_slot = 0;
    buffer->metadata_read_slot = 0;

    // Initialize the buffer event
    buffer->packets_waiting_in_buffer = CreateEvent(
        NULL,                                   // Default security attributes
        TRUE,                                   // Manual reset event!
        FALSE,                                  // Initially the event is NOT set.
        TEXT("PacketsAddedEvent")               // Event name
        );
}

/*
 *  Network state variable, encapsulating all information about the network.
 */
typedef struct network_state {
    // Sender -> Receiver Network
    NETWORK_PACKET_BUFFER outbound_NIC;
    NETWORK_PACKET_BUFFER network_buffer;
    NETWORK_PACKET_BUFFER inbound_NIC;

    // Thread handles
    HANDLE nic_to_wire_thread;
    HANDLE wire_to_nic_thread;

    // Thread IDs
    ULONG nic_to_wire_ID;
    ULONG wire_to_nic_ID;

    // State
    BOOL initialized;
} NETWORK_STATE, *PNETWORK_STATE;


// Our state objects, keeping track of all relevant information for our two networks!
NETWORK_STATE SR_net;
NETWORK_STATE RS_net;

PBUFFER_PACKET_METADATA get_empty_buffer_slot(PNETWORK_PACKET_BUFFER buffer) {

    PBUFFER_PACKET_METADATA packet_metadata;
    ULONG64 write_slot = buffer->metadata_write_slot;
    ULONG64 read_slot = buffer->metadata_read_slot;
    ULONG64 size = buffer->num_metadata_slots;
    ULONG64 write_slot_returned;

    // We check to make sure we have not filled in the entire buffer.
    while (read_slot == write_slot || write_slot % size != read_slot % size) {
        ASSERT(write_slot >= read_slot);

        // Attempt to grad the next write slot
        write_slot_returned = InterlockedCompareExchange64(
            (volatile PLONG64) &buffer->metadata_write_slot,
            (LONG64) write_slot + 1,
            (LONG64) write_slot
            );

        // If our original value is the same as the one returned, then we know we
        // succeeded in grabbing this slot!
        if (write_slot_returned == write_slot) {
            packet_metadata = &buffer->metadata_slots[write_slot % size];
            ASSERT(packet_metadata->status == EMPTY);
            packet_metadata->status = WRITING;
            return packet_metadata;
        }

        // Otherwise, we will update our variables and try again
        write_slot = buffer->metadata_write_slot;
        read_slot = buffer->metadata_read_slot;
    }

    // If we cannot find an empty NIC slot, return NO_NIC_SLOT_AVAILABLE
    return NO_BUFFER_SLOT_AVAILABLE;
}

/**
 *  Here, the receive_packet threads request a packet from the nic.
 *  This will check the read cursor from buffer. If there is a packet available,
 *  it will attempt to grab it using interlocked compare exchange on the metadata
 *  buffer read slot. If the returned value matches the original, then we know we succeeded
 *  in getting that slot. And the slot is increased by 1, which means no other thread
 *  can claim the same slot.
 */
PBUFFER_PACKET_METADATA try_get_packet_from_buffer(PNETWORK_PACKET_BUFFER buffer) {

    PBUFFER_PACKET_METADATA packet_metadata;
    ULONG64 current_write_slot = buffer->metadata_write_slot;
    ULONG64 current_read_slot = buffer->metadata_read_slot;
    ULONG64 size = buffer->num_metadata_slots;
    ULONG64 read_slot_returned;

    // If there are any packets available, we will attempt to grab the next one!
    while (current_write_slot != current_read_slot) {

        // We attempt to move the read cursor up one slot
        read_slot_returned = InterlockedCompareExchange64(
            (volatile PLONG64) &buffer->metadata_read_slot,
            (LONG64) current_read_slot + 1,
            (LONG64) current_read_slot
            );

        // If our original value is the same as the one returned, then we know we
        // succeeded in grabbing this slot!
        if (read_slot_returned == current_read_slot) {
            packet_metadata = &buffer->metadata_slots[current_read_slot % size];
            ASSERT(packet_metadata->status == READY);
            packet_metadata->status = READING;
            return packet_metadata;
        }

        // Otherwise, we will update our variables and try again
        current_write_slot = buffer->metadata_write_slot;
        current_read_slot = buffer->metadata_read_slot;
    }

    // If we cannot find an empty NIC slot, return NO_NIC_SLOT_AVAILABLE
    return NO_BUFFER_PACKET_AVAILABLE;
}

/*
 *  Manage transfers from the user's network card to the network. In this simulation, the network
 *  card and network are simply buffers. This worker thread moves packets from the NIC to the network
 *  as fast as it can. In doing so, it clears out slots on the NIC, allowing the sending party to
 *  enqueue more packets.
 *
 *  Edge case: if there are no slots available in the network, the packet is rejected.
 *
 *  Note: this thread will always take a set amount of time to emulate the "serialization delay"
 *  in the network: with a given bandwidth, data can be written on the wire at a specific speed.
 *  This thread will not exceed those speeds as defined in the configuration file.
 *
 *  Parameter: struct giving the specific memory buffer and the NIC this thread manages.
 */
void NIC_to_wire_thread(PNETWORK_STATE n) {

    // Create references
    PNIC_BUFFER nic = &n->outbound_NIC;
    PNETWORK_PACKET_BUFFER buffer = &n->network_buffer;

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = nic->packets_added_to_nic;
    events[EXIT_EVENT_INDEX] = simulation_end;

    // Create our helper variables
    ULONG64 slot = 0;
    ULONG64 row = 0;
    ULONG64 offset = 0;
    ULONG64 mask = 0;
    ULONG64 network_buffer_slot = 0;
    ULONG64 network_row = 0;
    ULONG64 network_offset = 0;
    BOOL net_bit;
    BOOL nic_reserve_bit;
    BOOL nic_ready_bit;
    BOOL consecutive_misses = 0;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(simulation_begin, INFINITE);

    while (TRUE) {

        // Wait for a signal -- we will run this thread after a certain amount of time has passed
        // or after we receive the packets_added_to_NIC event.
        // If we receive the exit simulation event, we immediately return.
        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, NET_RETRY_MS)
            == EXIT_EVENT_INDEX) return;

        consecutive_misses = 0;
        while (consecutive_misses < MAX_NIC_MISSES_BEFORE_SLEEP) {
            // Update our variables
            slot = slot % NIC_BUFFER_CAPACITY;
            row = slot / 64;
            offset = slot % 64;
            mask = (1ULL << offset);

            // Read in the relevant row of the bitmap
            ULONG64 row_snapshot = nic->packet_ready_lock[row];

            // If this slot is zero, then there is no packet in that slot. Continue to next slot.
            if (!(row_snapshot & mask)) {
                slot++;
                consecutive_misses++;
                continue;
            }

            // If the NIC slot IS set, then we will find a slot in our network buffer to write to.
            // We know the snapshot is valid because there is only ONE NIC-to-wire thread, and
            // this thread is the ONLY thread with the ability to clear bits in the packets ready lock.
            network_buffer_slot = get_empty_network_slot(buffer);
            network_row = network_buffer_slot / 64;
            network_offset = network_buffer_slot % 64;

            // TODO Wait for serialization delay

            // Now that we have a slot in the network, memcopy to it & timestamp for latency
            memcpy(
                &buffer->metadata_slots[network_buffer_slot].packet,
                &nic->packets[slot],
                sizeof(PACKET)
                );
            buffer->metadata_slots[network_buffer_slot].time_available =
                time_now_ms() + PROPAGATION_DELAY_MS;

            // Then, set the bit in the network bitmap lock
            net_bit = InterlockedBitTestAndSet64(
                &buffer->lock[network_row],
                network_offset);
            // The original value in the bitmap should be 0
            ASSERT(!net_bit);

            // Finally, clear the bits in the NIC locks to allow the slot to be filled
            nic_ready_bit = InterlockedBitTestAndReset64(
                &nic->packet_ready_lock[row],
                offset);
            // Original value should be 1
            ASSERT(nic_ready_bit);

            nic_reserve_bit = InterlockedBitTestAndReset64(
                &nic->reserve_slot_lock[row],
                offset);
            // Original bit should be set
            ASSERT(nic_reserve_bit);

            // Note that we wrote out a packet -- indicating that we should keep looking for more
            consecutive_misses = 0;
            slot++;
            SetEvent(buffer->packets_waiting_in_buffer);
        }

        // In this situation, there are no packets for us to write -- so we will reset this event
        // and begin to wait.
        ResetEvent(nic->packets_added_to_nic);
    }
}

/*
 *  Manage transfers from wire to NIC. In this simulation, once packets "arrive" at the other end
 *  of the network, they must be written into the user's NIC. Once they are written to the NIC (in
 *  this simulation, a buffer) that space is cleared in the network.
 *
 *  Edge case: if there are no available slots in the NIC, the packet is dropped.
 *
 *  Parameter: struct giving the specific memory buffer and the NIC this thread manages.
 */
void wire_to_NIC_thread(PNETWORK_STATE n) {

    // Create references
    PNIC_BUFFER nic = &n->inbound_NIC;
    PNETWORK_PACKET_BUFFER buffer = &n->network_buffer;

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = buffer->packets_waiting_in_buffer;
    events[EXIT_EVENT_INDEX] = simulation_end;

    // Create our helper variables
    ULONG64 earliest_eta = 0;
    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    PBUFFER_PACKET_METADATA entry;
    ULONG64 entry_eta;
    ULONG64 time_to_next_wakeup = 0;
    LONG64 nic_slot = 0;
    ULONG64 nic_row = 0;
    ULONG64 nic_offset = 0;
    BOOL net_bit;
    BOOL nic_ready_bit;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(simulation_begin, INFINITE);

    while (TRUE) {

        // Reset our variables
        earliest_eta = UINT64_MAX;

        // Scan network buffer, process arrived packets
        for (slot = 0; slot < NETWORK_BUFFER_CAPACITY_IN_BYTES; slot++) {

            // Update variables
            row = slot / 64;
            offset = slot % 64;
            mask = (1LL << offset);

            // Check this row. If it is all zeros, move on to the next one.
            if (buffer->lock[row] == 0) {
                slot = (row + 1) * 64 - 1;
                continue;
            }

            // Check this slot. If it is zero (false) then we move on to the next slot.
            if (!(buffer->lock[row] & mask)) continue;

            // Since this slot is set, there is a packet waiting. First, let's check its eta
            entry = &buffer->metadata_slots[slot];
            entry_eta = entry->time_available;

            // If this packet is unavailable, we will save its eta and move on to the next one
            if (entry_eta > time_now_ms()) {
                if (entry_eta > earliest_eta) earliest_eta = entry_eta;
                continue;
            }

            // We know the packet is ready to be moved to the receiving NIC!
            nic_slot = get_empty_nic_slot_inbound(nic);
            nic_row = nic_slot / 64;
            nic_offset = nic_slot % 64;

            // If no slots are available in the NIC, drop the packet.
            if (nic_slot == NO_BUFFER_SLOT_AVAILABLE) {
                // Clear the buffer lock bit and move on to the next slot
                net_bit = InterlockedBitTestAndReset64(
                    &buffer->lock[row],
                    offset);
                ASSERT(net_bit);
#if DEBUG
                printf("Inbound NIC full -- dropping packet %d\n", entry->packet.transmission_id);
#endif


                continue;
            }

            // We have a slot, so let's copy the data over
            memcpy(
                &nic->packets[nic_slot],
                &entry->packet,
                sizeof(PACKET)
                );

            // Then, alert the receive_packet threads by setting the lock bit
            nic_ready_bit = InterlockedBitTestAndSet64(
                &nic->packet_ready_lock[nic_row],
                nic_offset);
            ASSERT(!nic_ready_bit);

            // Set the packets available event (if not already set)
            SetEvent(nic->packets_added_to_nic);

            // Finally, clear the slot in the network buffer
            net_bit = InterlockedBitTestAndReset64(
                &buffer->lock[row],
                offset);
            ASSERT(net_bit);

            // Now that we're done with that slot -- move on to the next!
        }

        // If there is a packet that has become available since our search began,
        // let's loop back and get it!
        if (earliest_eta <= time_now_ms()) continue;

        // We will need to sleep some amount of time. Let's calculate it. First,
        // assume we sleep the maximum amount of time:
        time_to_next_wakeup = NET_RETRY_MS;

        // If no packets were found, we should reset the packets available event
        if (earliest_eta == UINT64_MAX) ResetEvent(buffer->packets_waiting_in_buffer);

        // If there are packets waiting, sleep until the next one is ready!
        else time_to_next_wakeup = earliest_eta - time_now_ms();

        // Wait for a signal -- we will run this thread after our timeout has expired
        // OR after we receive the packets_added_to_network event.
        // If we receive the exit simulation event, we immediately return.
        if (WaitForMultipleObjects(
            ARRAYSIZE(events),
            events,
            FALSE,
            time_to_next_wakeup)    == EXIT_EVENT_INDEX) return;
    }
}


/*  Initialize all buffers for the given network.
 */
void net_init(PNETWORK_STATE n) {

    initialize_network_packet_buffer(
        &n->inbound_NIC,
        NIC_BUFFER_TOTAL_PACKET_SLOTS,
        NIC_BUFFER_CAPACITY_IN_BYTES
        );

    initialize_network_packet_buffer(
        &n->network_buffer,
        NETWORK_BUFFER_TOTAL_PACKET_SLOTS,
        NETWORK_BUFFER_CAPACITY_IN_BYTES
        );

    initialize_network_packet_buffer(
        &n->outbound_NIC,
        NIC_BUFFER_TOTAL_PACKET_SLOTS,
        NIC_BUFFER_CAPACITY_IN_BYTES
        );

    // Create nic to wire and wire to nic threads
    n->nic_to_wire_thread = CreateThread (
            DEFAULT_SECURITY,
            DEFAULT_STACK_SIZE,
            (LPTHREAD_START_ROUTINE) NIC_to_wire_thread,
            n,                                   // Network state pointer is passed as a parameter
            DEFAULT_CREATION_FLAGS,
            &n->nic_to_wire_ID
            );
    ASSERT(n->nic_to_wire_thread);

    n->wire_to_nic_thread = CreateThread (
            DEFAULT_SECURITY,
            DEFAULT_STACK_SIZE,
            (LPTHREAD_START_ROUTINE) wire_to_NIC_thread,
            n,                                   // Network state pointer is passed as a parameter
            DEFAULT_CREATION_FLAGS,
            &n->wire_to_nic_ID
            );
    ASSERT(n->nic_to_wire_thread);

    n->initialized = TRUE;
}

void buffer_free(NETWORK_PACKET_BUFFER buffer) {

    // Free dynamically-allocated data
    free(buffer.metadata_slots);
    free(buffer.buffer_data);

    // Close each event handle
    CloseHandle(buffer.packets_waiting_in_buffer);
}

void net_free(PNETWORK_STATE n) {
    // Wait for all threads to finish running
    WaitForSingleObject(n->nic_to_wire_thread, INFINITE);
    WaitForSingleObject(n->wire_to_nic_thread, INFINITE);

    // Free buffer data
    buffer_free(n->inbound_NIC);
    buffer_free(n->network_buffer);
    buffer_free(n->outbound_NIC);
}

/*
 * create_network_layer
 *
 * Initializes the entire network layer: one network state for sender to receiver
 * and one network state for receiver to sender.
 */
void create_network_layer(void) {

    // Initialize networks
    net_init(&SR_net);
    net_init(&RS_net);
}

/**
 * @brief Frees network layer resources.
 **/
void free_network_layer(void) {
    net_free(&SR_net);
    net_free(&RS_net);
}

BOOL acquire_buffer_space(
    PBUFFER_PACKET_METADATA nic_packet,
    PNETWORK_PACKET_BUFFER nic
    ) {

    // First, we will check the slot behind ours
    // (and we must be careful to wrap around)

    // We will build off of its byte offset + its size.

    // If we can fit in the space following it, we will write into that space.
    // We need to check for two things:
        // 1. Does this reach the beginning of the next packet -- the read packet?
        //      If it does, then we will need to reject this packet.

        // 2. Does this go out of bounds? If so, then we will need to start from
        //      location 0 and check #1 again.

    return TRUE;
}

/*
 * send_packet
 *
 * Sends a packet through the simulated network.
 */
int send_packet(PPACKET pkt, int role) {

    // Validate inputs to ensure proper usage
    if (pkt == NULL)                                    return PACKET_REJECTED;
    if (pkt->bytes_in_payload > MAX_PAYLOAD_SIZE)       return PACKET_REJECTED;
    if (role != ROLE_SENDER && role != ROLE_RECEIVER)   return PACKET_REJECTED;

    // TODO: Apply network unreliability (drop, duplicate, corrupt, reorder)

    // Allocate all necessary stack variables
    PNETWORK_STATE n;
    PNETWORK_PACKET_BUFFER nic;
    PBUFFER_PACKET_METADATA nic_packet;

    // Select network based on role
    n = &SR_net;
    if (role == ROLE_RECEIVER) n = &RS_net;
    nic = &n->outbound_NIC;

    // Find an available NIC slot
    nic_packet = get_empty_buffer_slot(nic);

    // If no slots are available, reject the packet
    if (!nic_packet) return PACKET_REJECTED;

    // We will acquire the space in the data buffer for the packet.
    // If this failes for any reason, we will release the metadata slot
    // and return.
    if (!acquire_buffer_space(nic_packet, nic)) return PACKET_REJECTED;

    // Write data to NIC buffer
    __try {
        memcpy(
            nic_packet->location_in_buffer,
            pkt,
            nic_packet->bytes_in_buffer
            );
    }

    // If the memcpy fails, then there must be a problem with the pointer
    // passed in from the transport layer. We will reject the packet
    // and return.
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("Error copying from transport packet.\n");
        nic_packet->status = EMPTY;
        return PACKET_REJECTED;
    }

    // Otherwise, we were able to successfully write the packet into the buffer.
    // We will set the event, if necessary, to wake any waiting threads.
    nic_packet->status = READY;
    SetEvent(nic->packets_waiting_in_buffer);
    return PACKET_ACCEPTED;
}

/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
int receive_packet(PPACKET pkt, ULONG64 timeout_ms, int role) {

    // First, we check for all necessary validations
    if (pkt == NULL)                                    return NO_PACKET_AVAILABLE;
    if (role != ROLE_SENDER && role != ROLE_RECEIVER)   return NO_PACKET_AVAILABLE;

    // Allocate all necessary stack variables
    PNETWORK_STATE n;
    PNETWORK_PACKET_BUFFER nic;
    ULONG64 deadline;
    PBUFFER_PACKET_METADATA nic_packet;

    // Then we determine which network state to select
    n = &SR_net;
    if (role == ROLE_SENDER) n = &RS_net;
    nic = &n->inbound_NIC;

    // Keep track of time
    deadline = time_now_ms() + timeout_ms;

    while (TRUE) {

        // Find an available packet in the NIC
        nic_packet = try_get_packet_from_buffer(nic);

        // If we were able to get a packet,
        // then we will send its data up to the transport layer.
        if (nic_packet) {
            ASSERT(nic_packet->status == READING);
            __try {
                // Copy the data to the given packet pointer
                memcpy(
                    pkt,
                    nic_packet->location_in_buffer,
                    nic_packet->bytes_in_buffer
                    );
            }
            // If the memcopy fails, we assume a bad actor on the transport layer,
            // And we reject the packet.
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("Error copying data to transport packet\n");
                // Free the slot in the buffer
                nic_packet->status = EMPTY;
                return PACKET_REJECTED;
            }

            // Free the slot in the buffer
            nic_packet->status = EMPTY;

            // Success! One packet was received. We can now return.
            return PACKET_RECEIVED;
        }

        // If no packets are available, we will wait for one.
        ResetEvent(nic->packets_waiting_in_buffer);
        WaitForSingleObject(nic->packets_waiting_in_buffer, NET_RETRY_MS);

        // Check for a timeout
        if (time_now_ms() > deadline) return NO_PACKET_AVAILABLE;

        // If we don't have a timeout, then we will scan the NIC again!
    }
}

/*
 * try_receive_packet
 *
 * Attempts to receive a packet without waiting.
 */
int try_receive_packet(PPACKET pkt, int role) {
    return receive_packet(pkt, 0, role);
}