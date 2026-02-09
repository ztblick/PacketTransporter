/*
 * network.c
 * 
 * Network Layer Implementation
 */

// TODO test new implementation of network layer using previous net_tester file.

#include "network.h"
#include "network_packets.h"

// These are our statuses for the metadata slots
enum {EMPTY, RESERVED, WRITING, READY, READING};

/*
 * Buffer entry objects - include packet and available time
 */
typedef struct buffer_packet_metadata {
    PBYTE starting_address_in_buffer;
    ULONG64 packet_size_in_bytes;
    volatile LONG64 status;
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

// TODO combine this with the function that reserves the space, too
PBUFFER_PACKET_METADATA get_empty_buffer_slot(PNETWORK_PACKET_BUFFER buffer) {

    PBUFFER_PACKET_METADATA packet_metadata;
    ULONG64 write_slot = buffer->metadata_write_slot;
    ULONG64 read_slot = buffer->metadata_read_slot;
    ULONG64 size = buffer->num_metadata_slots;
    LONG64 status_returned;

    // We check to make sure we have not filled in the entire buffer.
    while (read_slot == write_slot || write_slot % size != read_slot % size) {
        ASSERT(write_slot >= read_slot);

        // Attempt to grad the next write slot by setting its status
        status_returned = InterlockedCompareExchange64(
            &buffer->metadata_slots[write_slot % size].status,
            RESERVED,
            EMPTY
            );

        // If our original value is the same as the one returned, then we know we
        // succeeded in grabbing this slot!
        if (status_returned == EMPTY) {
            packet_metadata = &buffer->metadata_slots[write_slot % size];

            // Before we exit, we will advance the read pointer for any other threads
            InterlockedIncrement64((PLONG64) &buffer->metadata_write_slot);

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
 **/
PBUFFER_PACKET_METADATA try_get_packet_from_buffer(PNETWORK_PACKET_BUFFER buffer) {

    PBUFFER_PACKET_METADATA packet_metadata;
    ULONG64 write_slot = buffer->metadata_write_slot;
    ULONG64 read_slot = buffer->metadata_read_slot;
    ULONG64 size = buffer->num_metadata_slots;
    LONG64 status_returned;

    // TODO resolve bug here -- there are situations where this can get stuck.
    // If there are any packets available, we will attempt to grab the next one!
    while (write_slot != read_slot) {

        // We attempt to move the read cursor up one slot
        status_returned = InterlockedCompareExchange64(
            &buffer->metadata_slots[read_slot % size].status,
            READING,
            READY
            );

        // If our original value is the same as the one returned, then we know we
        // succeeded in grabbing this slot!
        if (status_returned == READY) return &buffer->metadata_slots[read_slot % size];

        // Otherwise, we will update our variables and try again
        write_slot = buffer->metadata_write_slot;
        read_slot = buffer->metadata_read_slot;
    }

    // If there is no packet available in the buffer, we return that status.
    return NO_BUFFER_PACKET_AVAILABLE;
}


/**
 * Finds the byte offset in the buffer for the new packet. Checks for out of bounds or overwriting
 * waiting packets.
 *
 * @param packet_metadata The packet's metadata slot into which we write its byte offset.
 * @param buffer The buffer into which we will write the data for this packet.
 * @return
 **/
BOOL acquire_buffer_space(  PBUFFER_PACKET_METADATA packet_metadata,
                            PNETWORK_PACKET_BUFFER buffer               ) {

    PBUFFER_PACKET_METADATA previous_packet;
    PBYTE my_location;
    ULONG64 my_size_in_bytes = packet_metadata->packet_size_in_bytes;
    PBUFFER_PACKET_METADATA read_packet;
    PBYTE read_start;
    ULONG64 size = buffer->num_metadata_slots;
    UINT8 attempts = 0;

    // First, we will check the slot behind ours
    previous_packet = packet_metadata - 1;

    // If it turns out that our packet was the very first packet, then we need to look at the
    // packet at the very end of the slots
    if (previous_packet < buffer->metadata_slots)
        previous_packet = buffer->metadata_slots + (buffer->num_metadata_slots - 1);

    // It is important for us to initialize the location for this slot.
    // Even if we return false and do not end up writing any data to the buffer,
    // we want this slot to have a value for its pointer so the next
    // slot can build off of it.
    my_location = previous_packet->starting_address_in_buffer;

    /**
     * Our packet will begin where the previous packet ends.
     * To figure out what to do, we need to check the previous packet's
     * status. There are three possibilities:
     * 1. Its status is WRITING, READY, or READING. In these cases,
     * its size has been set. We can use its size reliably.
     * 2. Its status is RESERVED. This means its size has not yet been set.
     *    We need to wait for its status to become WRITING or EMPTY.
     * 3. Its status is empty. In this case, we do not need to worry about
     * the packet, as it has been rejected and will take up no space in the buffer.
     * We will begin from its starting location.
     *
     **/

    while (previous_packet->status == RESERVED) {
        attempts++;
        if (attempts == MAX_ATTEMPTS) goto failure;
    }

    // If the previous packet does not have a location (i.e. this is the first packet ever!)
    // then we will initialize our pointer to the start of the buffer
    if (previous_packet->starting_address_in_buffer == 0)
        my_location = buffer->buffer_data;
    else
        my_location += previous_packet->packet_size_in_bytes;

    /**
     * Now that we know where we begin, we need to check a few things.
     *  1. Does this reach the beginning of the next packet -- the read packet?
     *      If it does, then we will need to reject this packet.
     *
     *  The next packet could be ahead of us. It could also be behind us.
     *
     *  A. Ahead of us and we fit in the buffer => all good!
     *  B. Ahead of us and we run into it => retun false
     *  C. Behind us and we fit in the buffer => all good!
     *  D. Behind us and we do not fit --> we need to wrap around and check again.
     **/

    read_packet = &buffer->metadata_slots[buffer->metadata_read_slot % size];
    read_start = read_packet->starting_address_in_buffer;

    // First, we will handle the case where the packet is ahead of us in the buffer.
    if (read_start > my_location) {
        // If I overlap with the next packet, return false
        if (my_location + my_size_in_bytes > read_start) goto failure;
        // Otherwise, set the location and return true!
        goto success;
    }

    // Now, we know the read packet it behind us. In this case, we will check
    // to see if we fit in the buffer. If we do, then we return true!
    if (my_location + my_size_in_bytes <
        buffer->buffer_data + buffer->buffer_size_in_bytes ) goto success;

    // Now the tricky case: we will check to see if we can wrap around.
    // If we can, then we reset our location in the buffer and return true.
    my_location = buffer->buffer_data;
    if (my_location + my_size_in_bytes > read_start) goto failure;
    goto success;

    success:
        packet_metadata->starting_address_in_buffer = my_location;
        return TRUE;

    failure:
        packet_metadata->starting_address_in_buffer = my_location;
        return FALSE;
}

/**
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
    PNETWORK_PACKET_BUFFER nic_buffer = &n->outbound_NIC;
    PNETWORK_PACKET_BUFFER net_buffer = &n->network_buffer;

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = nic_buffer->packets_waiting_in_buffer;
    events[EXIT_EVENT_INDEX] = simulation_end;
    PBUFFER_PACKET_METADATA nic_packet;
    PBUFFER_PACKET_METADATA net_packet;


    // Wait for system start event before entering waiting state!
    WaitForSingleObject(simulation_begin, INFINITE);

    while (TRUE) {

        // Wait for a signal -- we will run this thread after a certain amount of time has passed
        // or after we receive the packets_added_to_NIC event.
        // If we receive the exit simulation event, we immediately return.
        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, NET_RETRY_MS)
            == EXIT_EVENT_INDEX) return;

        while (TRUE) {

            // Find a packet on the NIC to move
            nic_packet = try_get_packet_from_buffer(nic_buffer);
            if (!nic_packet) break;

            // Find a slot on the wire to move into
            net_packet = get_empty_buffer_slot(net_buffer);
            if (!net_packet) {
                printf("Fatal error: network overflow.\n");
                ASSERT(FALSE);
                return;
            }
            net_packet->packet_size_in_bytes = nic_packet->packet_size_in_bytes;
            net_packet->status = WRITING;

            if (!acquire_buffer_space(net_packet, net_buffer)) {
                printf("Fatal error: network overflow.\n");
                ASSERT(FALSE);
                return;
            }

            // Memcopy!
            memcpy(
                net_packet->starting_address_in_buffer,
                nic_packet->starting_address_in_buffer,
                nic_packet->packet_size_in_bytes
                );

            // Update data as necessary
            net_packet->time_available = time_now_ms() + LATENCY_MS;
            net_packet->status = READY;

            // Now we must advance the read pointer in the buffer, as we are done with this packet!
            InterlockedIncrement64((PLONG64) &nic_buffer->metadata_read_slot);

            // Free the slot in the buffer
            InterlockedExchange64(&nic_packet->status, EMPTY);

            // Note that we wrote out a packet -- indicating that we should keep looking for more
            SetEvent(net_buffer->packets_waiting_in_buffer);
        }

        // In this situation, there are no packets for us to write -- so we will reset this event
        // and begin to wait.
        ResetEvent(nic_buffer->packets_waiting_in_buffer);
    }
}

/**
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
    PNETWORK_PACKET_BUFFER nic_buffer = &n->inbound_NIC;
    PNETWORK_PACKET_BUFFER net_buffer = &n->network_buffer;

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = net_buffer->packets_waiting_in_buffer;
    events[EXIT_EVENT_INDEX] = simulation_end;

    // Create our helper variables
    ULONG64 next_time_available = 0;
    PBUFFER_PACKET_METADATA net_packet;
    PBUFFER_PACKET_METADATA nic_packet;
    ULONG64 entry_eta;
    ULONG64 time_to_next_wakeup = 0;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(simulation_begin, INFINITE);

    while (TRUE) {
        // Reset our variables
        next_time_available = UINT64_MAX;

        while (TRUE) {
            // Look at the next available packet in the net buffer, if one exists
            net_packet = try_get_packet_from_buffer(net_buffer);
            if (!net_packet) break;

            // If it's not ready, set next_time_available and be sure to keep
            // the network read pointer in the same spot
            entry_eta = net_packet->time_available;
            if (entry_eta > time_now_ms()) {
                next_time_available = entry_eta;
                break;
            }

            // If it's ready but there is no space in the NIC, drop it
            // and update network metadata
            nic_packet = get_empty_buffer_slot(nic_buffer);
            if (!nic_packet) {
                net_packet->status = EMPTY;
                net_buffer->metadata_read_slot++;
                continue;
            }

            nic_packet->packet_size_in_bytes = net_packet->packet_size_in_bytes;
            InterlockedExchange64(&nic_packet->status, WRITING);

            if (!acquire_buffer_space(net_packet, nic_buffer)) {
                InterlockedExchange64(&nic_packet->status, EMPTY);
                net_packet->status = EMPTY;
                net_buffer->metadata_read_slot++;
                continue;
            }

            // If you can write it to the NIC, do it!
            // Update network and NIC metadata
            memcpy(
                nic_packet->starting_address_in_buffer,
                net_packet->starting_address_in_buffer,
                net_packet->packet_size_in_bytes
                );

            net_buffer->metadata_read_slot++;
            net_packet->status = EMPTY;
            InterlockedExchange64(&nic_packet->status, READY);
            SetEvent(nic_buffer->packets_waiting_in_buffer);
        }

        // If there is a packet that has become available since our search began,
        // let's loop back and get it!
        if (next_time_available <= time_now_ms()) continue;

        // We will need to sleep some amount of time. Let's calculate it. First,
        // assume we sleep the maximum amount of time:
        time_to_next_wakeup = NET_RETRY_MS;

        // If no packets were found, we should reset the packets available event
        if (next_time_available == UINT64_MAX) ResetEvent(net_buffer->packets_waiting_in_buffer);

        // If there are packets waiting, sleep until the next one is ready!
        else time_to_next_wakeup = next_time_available - time_now_ms();

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
    ULONG64 total_packet_size_in_bytes = 0;
    PDC_HEADER packet_info;
    ULONG64 packet_universal_header_size_in_bytes = 0;
    ULONG64 packet_data_header_size_in_bytes = 0;
    ULONG64 packet_payload_size_in_bytes = 0;

    // Find the size of the entire packet and update its metadata field.
    packet_universal_header_size_in_bytes = pkt->total_bytes_in_packet_header;
    packet_info = (PDC_HEADER) ((ULONG_PTR) pkt + pkt->total_bytes_in_packet_header);
    packet_data_header_size_in_bytes = packet_info->total_bytes_in_dc_header;
    packet_payload_size_in_bytes = pkt->bytes_in_payload;

    // Check given values for security purposes -- the sum must not wrap
    if (packet_universal_header_size_in_bytes > UINT64_MAX - packet_data_header_size_in_bytes)
        return PACKET_REJECTED;

    total_packet_size_in_bytes = packet_universal_header_size_in_bytes + packet_data_header_size_in_bytes;
    if (packet_payload_size_in_bytes > UINT64_MAX - total_packet_size_in_bytes)
        return PACKET_REJECTED;

    total_packet_size_in_bytes += packet_payload_size_in_bytes;

    // Select network based on role
    n = &SR_net;
    if (role == ROLE_RECEIVER) n = &RS_net;
    nic = &n->outbound_NIC;

    // Find an available NIC slot
    nic_packet = get_empty_buffer_slot(nic);

    // If no slots are available, reject the packet
    if (!nic_packet) {
        ASSERT(FALSE);
        return PACKET_REJECTED;
    }
    // Now we update our packet metadata with its total size -- this information is used to
    // determine if it can fit into the buffer.
    nic_packet->packet_size_in_bytes = total_packet_size_in_bytes;
    InterlockedExchange64(&nic_packet->status, WRITING);

    // We will acquire the space in the data buffer for the packet.
    // If this fails for any reason, we will release the metadata slot
    // and return.
    if (!acquire_buffer_space(nic_packet, nic)) {
        InterlockedExchange64(&nic_packet->status, EMPTY);
        ASSERT(FALSE);
        return PACKET_REJECTED;
    }

    // Write data to NIC buffer
    __try {
        memcpy(
            nic_packet->starting_address_in_buffer,
            pkt,
            nic_packet->packet_size_in_bytes
            );
    }

    // If the memcpy fails, then there must be a problem with the pointer
    // passed in from the transport layer. We will reject the packet
    // and return.
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("Error copying from transport packet.\n");
        InterlockedExchange64(&nic_packet->status, EMPTY);
        ASSERT(FALSE);
        return PACKET_REJECTED;
    }

    // Otherwise, we were able to successfully write the packet into the buffer.
    // We will set the event, if necessary, to wake any waiting threads.
    InterlockedExchange64(&nic_packet->status, READY);
    SetEvent(nic->packets_waiting_in_buffer);

    return PACKET_ACCEPTED;
}

/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
// TODO we are failing to receive for some reason...
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
                    nic_packet->starting_address_in_buffer,
                    nic_packet->packet_size_in_bytes
                    );
            }
            // If the memcopy fails, we assume a bad actor on the transport layer,
            // And we reject the packet.
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("Error copying data to transport packet\n");
                // We failed copying this packet's data, so we will change its status in the buffer.
                InterlockedExchange64(&nic_packet->status, READY);
                return PACKET_REJECTED;
            }

            // Now we must advance the read pointer in the buffer, as we are done with this packet!
            InterlockedIncrement64((PLONG64) &nic->metadata_read_slot);

            // Free the slot in the buffer
            InterlockedExchange64(&nic_packet->status, EMPTY);

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