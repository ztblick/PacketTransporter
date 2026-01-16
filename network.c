/*
 * network.c
 * 
 * Network Layer Implementation
 */

#include "network.h"

/*
 * Buffer entry objects - include packet and available time
 */
typedef struct buffer_entry {
    ULONG64 time_available;
    PACKET packet;
} BUFFER_ENTRY, *PBUFFER_ENTRY;

/*
 * Network buffer: a contiguous region of memory into which packets are copied. All packets are
 * given a timestamp of current_time + latency. This is used to determine the arrival time of the
 * packet.
 *
 * Each has a bitmap which serves as a lock on the slot. When the NIC->network thread sees a 0,
 * it can claim that slot. Since there is only one thread, it can write to that slot safely.
 * When finished, it will use an interlocked operation to set that bit, which tells the net->NIC
 * thread that the slot is full and can be read from.
 */
typedef struct network_buffer {

    BUFFER_ENTRY packets[NETWORK_BUFFER_CAPACITY];

    // We want one bit per packet and a minimum of one "row" in our bitmap. So we add 63 to always
    // have at least one row without adding an extra row superfluously (e.g. 1 packet capacity = 1 ULONG64,
    // 63 packet capacity = 1 ULONG64, 127 packets = 2 ULONG64, 128 -> 2, 129 -> 3, etc.)
    volatile LONG64 lock[NETWORK_BITMAP_ROWS];

    // When this buffer's event is set, a thread waiting for
    // a packet is woken. This is a manual reset event, as
    // threads should continue to consume packets until there are
    // NONE left.
    HANDLE packets_added_to_network;
} NETWORK_BUFFER, *PNETWORK_BUFFER;

/*
 *  Initialize a network buffer object.
 */
void initialize_network_buffer(PNETWORK_BUFFER buffer) {

    memset(buffer->packets, 0, NETWORK_BUFFER_CAPACITY * sizeof(BUFFER_ENTRY));
    memset((void*)buffer->lock, 0, NETWORK_BITMAP_ROWS * sizeof(LONG64));

    buffer->packets_added_to_network = CreateEvent(
        NULL,                                   // Default security attributes
        TRUE,                                   // Manual reset event!
        FALSE,                                  // Initially the event is NOT set.
        TEXT("PacketsAddedToNetworkEvent")      // Event name
        );
}

/*
 *  NIC outbound buffer: this is very similar to the network buffer, only smaller. In this case, though, since
 *  MULTIPLE threads can be expected to write to the NIC buffer at once, we will need two bitmaps:
 *  The first will protect a slot from concurrent writes from multiple send_packet threads. When a
 *  sending thread reserves a slot (using an interlocked compare/exchange), that slot will be unavailable
 *  to other sending threads. Then, once the sending thread has finished its memcopy, it will set the
 *  bit in the OTHER bitmap, which alerts the NIC->network thread that the given slot is available for pickup.

 */
typedef struct nic_buffer {

    PACKET packets[NIC_BUFFER_CAPACITY];
    // Sending threads set reserve_slot to 1, write their packet, then set packet_ready to 1
    volatile LONG64 reserve_slot_lock[NIC_BITMAP_ROWS];
    // NIC->network finds a 1 in packet_ready, writes its data to the network, then clears the
    // packet_ready bit and then the reserve_slot bit
    volatile LONG64 packet_ready_lock[NIC_BITMAP_ROWS];

    HANDLE packets_added_to_nic;

} NIC_BUFFER, *PNIC_BUFFER;

/*
 *  Net_to_NIC buffer: this buffer represents the NIC of the receiving machine. In this case, only one thread fills
 *  slots, but multiple transport threads can clear slots. So, to facilitate this, the network thread writes its
 *  data to an empty slot (represented by a 0 in the packet_ready_lock lock), then sets that bit to 1. Then, a receiver
 *  thread may come along and see a 1 in the packet ready lock. If it can successfully set that corresponding bit
 *  in the reserve_slot lock, then they know they have that packet. They transfer the data, then reset both bits to 0.
 */

void initialize_nic_buffer(PNIC_BUFFER buffer) {
    memset(buffer->packets, 0, NIC_BUFFER_CAPACITY * sizeof(BUFFER_ENTRY));
    memset((void*)buffer->reserve_slot_lock, 0, NIC_BITMAP_ROWS * sizeof(LONG64));
    memset((void*)buffer->packet_ready_lock, 0, NIC_BITMAP_ROWS * sizeof(LONG64));

    buffer->packets_added_to_nic = CreateEvent(
        NULL,                                   // Default security attributes
        TRUE,                                   // Manual reset event!
        FALSE,                                  // Initially the event is NOT set.
        TEXT("PacketsAddedToNICEvent")          // Event name
        );
}

/*
 *  Network state variable, encapsulating all information about the network.
 */
typedef struct network_state {
    // Sender -> Receiver Network
    NIC_BUFFER outbound_NIC;
    NETWORK_BUFFER network_buffer;
    NIC_BUFFER inbound_NIC;

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

#if DEBUG
uint32_t packetStates[TOTAL_PACKETS_MULTITHREADED];
#endif

ULONG64 get_empty_network_slot(PNETWORK_BUFFER n) {

    ULONG64 slot = 0;
    ULONG64 row = 0;
    ULONG64 offset = 0;
    ULONG64 mask = 0;

    while (slot < NETWORK_BUFFER_CAPACITY) {
        // Update our variables
        row = slot / 64;
        offset = slot % 64;
        mask = (1ULL << offset);

        // Skip the whole row if it is all set
        if (n->lock[row] == BITMAP_ROW_FULL_VALUE) {
            slot = (row + 1) * 64;
            continue;
        }

        // If this operation returns a nonzero value, then the bit is set. Move along to the next slot.
        // If it returns 0, then the bit is cleared -- and we have found an empty slot!
        // Doing this without an interlocked operation is okay because there are only two threads
        // with access to the data, and the other thread (the wire to nic thread) only clears bits.
        // It never sets them. So this 0 is a reliable value.
        if (!(n->lock[row] & mask)) return slot;

        slot++;
    }

    // If we cannot find an empty network slot, we would want to resize. But, for now, let's
    // throw an error.
    ASSERT(FALSE);
    return -1;
}

LONG64 get_empty_nic_slot_outbound(PNIC_BUFFER n) {

    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 row_snapshot = 0;
    LONG64 mask = 0;
    BOOL bit_value;

    while (slot < NIC_BUFFER_CAPACITY) {
        // Update our variables
        row = slot / 64;
        offset = slot % 64;
        row_snapshot = n->reserve_slot_lock[row];
        mask = (1LL << offset);

        // Skip the whole row if it is all set
        if (row_snapshot == BITMAP_ROW_FULL_VALUE) {
            slot = (row + 1) * 64;
            continue;
        }

        // If this bit is set, move on to the next slot
        if (row_snapshot & mask) {
            slot++;
            continue;
        }

        // If this operation returns a nonzero value, then the bit is set. Move along to the next slot.
        // If it returns 0, then we need to be careful. Multiple user threads are here
        // simultaneously, so we need to make sure we don't wipe our their changes. So,
        // we can do an interlocked bit test and set. If the returned value is 1, then
        // another thread beat us to the slot. If not, then we got there first and the slot is ours!
        bit_value = InterlockedBitTestAndSet64(
            &n->reserve_slot_lock[row],
            offset);
        // If the bit was NOT already set -- then the slot is ours!
        if (!bit_value) return slot;

        // If the bit was set, then someone else beat us to it. Oh well!
        // In this case, we move on to the next slot.
        slot++;
    }

    // If we cannot find an empty NIC slot, return NO_NIC_SLOT_AVAILABLE
    return NO_NIC_SLOT_AVAILABLE;
}


LONG64 get_empty_nic_slot_inbound(PNIC_BUFFER n) {
    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    LONG64 row_snapshot = 0;
    BOOL bit_value;

    while (slot < NIC_BUFFER_CAPACITY) {
        // Update our variables
        row = slot / 64;
        offset = slot % 64;
        mask = (1LL << offset);
        row_snapshot = n->reserve_slot_lock[row];

        // Skip the whole row if it is all set
        if (row_snapshot == BITMAP_ROW_FULL_VALUE) {
            slot = (row + 1) * 64;
            continue;
        }

        // If this bit is already set, continue to the next slot
        if (row_snapshot & mask) {
            slot++;
            continue;
        }

        // We are the only thread writing from the network buffer to the nic. So, we need to find
        // an empty slot, then write to it. We only need to find a 0. No transport thread changes 0 to 1,
        // so that data will persist.
        bit_value = InterlockedBitTestAndSet64(
            &n->reserve_slot_lock[row],
            offset);
        ASSERT(!bit_value);
        return slot;
    }

    // If we cannot find an empty NIC slot, return NO_NIC_SLOT_AVAILABLE
    return NO_NIC_SLOT_AVAILABLE;
}

/*  Here, the receive_packet threads scan the nic for a filled slot in the
 *  packet_ready lock for the nic. If this thread can clear that bit before
 *  anyone else, then this thread has won the race for that packet.
 */
LONG64 find_filled_nic_slot(PNIC_BUFFER n) {
    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    LONG64 row_snapshot = 0;
    BOOL bit_value;

    while (slot < NIC_BUFFER_CAPACITY) {
        // Update our variables
        row = slot / 64;
        offset = slot % 64;
        mask = (1LL << offset);
        row_snapshot = n->packet_ready_lock[row];

        // Skip the whole row if entirely empty
        if (row_snapshot == 0) {
            slot = (row + 1) * 64;
            continue;
        }

        // If this slot is empty, move on to the next
        if (!(row_snapshot & mask)) {
            slot++;
            continue;
        }

        // Otherwise, we will try to grab this slot! Another thread might beat us,
        // so we will check the return value of the atomic bit reset.
        bit_value = InterlockedBitTestAndReset64(
                &n->packet_ready_lock[row],
                offset);

        // If the original bit was set, then we won the race for the slot!
        if (bit_value) return slot;

        // Otherwise, move on to the next slot
        slot++;
    }

    // If we cannot find an empty NIC slot, return NO_NIC_SLOT_AVAILABLE
    return NO_NIC_SLOT_AVAILABLE;
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
    PNETWORK_BUFFER buffer = &n->network_buffer;

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
                &buffer->packets[network_buffer_slot].packet,
                &nic->packets[slot],
                sizeof(PACKET)
                );
            buffer->packets[network_buffer_slot].time_available =
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
            SetEvent(buffer->packets_added_to_network);
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
    PNETWORK_BUFFER buffer = &n->network_buffer;

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = buffer->packets_added_to_network;
    events[EXIT_EVENT_INDEX] = simulation_end;

    // Create our helper variables
    ULONG64 earliest_eta = 0;
    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    PBUFFER_ENTRY entry;
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
        for (slot = 0; slot < NETWORK_BUFFER_CAPACITY; slot++) {

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
            entry = &buffer->packets[slot];
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
            if (nic_slot == NO_NIC_SLOT_AVAILABLE) {
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
        if (earliest_eta == UINT64_MAX) ResetEvent(buffer->packets_added_to_network);

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

    initialize_nic_buffer(&n->inbound_NIC);
    initialize_network_buffer(&n->network_buffer);
    initialize_nic_buffer(&n->outbound_NIC);

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

void net_free(PNETWORK_STATE n) {
    // Wait for all threads to finish running
    WaitForSingleObject(n->nic_to_wire_thread, INFINITE);
    WaitForSingleObject(n->wire_to_nic_thread, INFINITE);

    // Free all data and clear all events associated with this network state
    CloseHandle(n->outbound_NIC.packets_added_to_nic);
    CloseHandle(n->network_buffer.packets_added_to_network);
    CloseHandle(n->inbound_NIC.packets_added_to_nic);
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

#if DEBUG
    memset(packetStates, UNSENT, TOTAL_PACKETS_MULTITHREADED * sizeof(uint32_t));
#endif
}


/*
 * network_cleanup
 *
 * Frees network layer resources.
 */
void network_cleanup(void) {
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
    if (pkt->length > MAX_PAYLOAD_SIZE)                 return PACKET_REJECTED;
    if (role != ROLE_SENDER && role != ROLE_RECEIVER)   return PACKET_REJECTED;

    // TODO: Apply network unreliability (drop, duplicate, corrupt, reorder)

    // Select network based on role
    PNETWORK_STATE n = &SR_net;
    if (role == ROLE_RECEIVER) n = &RS_net;
    PNIC_BUFFER nic = &n->outbound_NIC;

    // Create our stack variables
    LONG64 nic_slot;
    LONG64 offset;
    LONG64 row;
    BOOL ready_bit;

    // Find an available NIC slot by checking reserve lock
    nic_slot = get_empty_nic_slot_outbound(nic);
    if (nic_slot == NO_NIC_SLOT_AVAILABLE) return PACKET_REJECTED;
    row = nic_slot / 64;
    offset = nic_slot % 64;

    // Write data to NIC buffer
    memcpy(
        &nic->packets[nic_slot],
        pkt,
        sizeof(PACKET)
        );

    // Set packet ready lock bit (interlocked compare exchange, again)
    ready_bit = InterlockedBitTestAndSet64(
        &nic->packet_ready_lock[row],
        offset);
    // This bit must be zero before this operation
    ASSERT(!ready_bit);

    // Signal the NIC->net thread
    SetEvent(nic->packets_added_to_nic);

    return PACKET_ACCEPTED;
}


/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
int receive_packet(PPACKET pkt, ULONG64 timeout_ms, int role) {
    if (pkt == NULL) {
        return NO_PACKET_AVAILABLE;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return NO_PACKET_AVAILABLE;
    }

    // Determine which network state you are in
    PNETWORK_STATE n = &SR_net;
    if (role == ROLE_SENDER) n = &RS_net;
    PNIC_BUFFER nic = &n->inbound_NIC;

    // Create our stack variables
    LONG64 nic_slot = 0;
    LONG64 offset = 0;
    LONG64 row = 0;
    BOOL ready_bit;

    // Keep track of time
    ULONG64 deadline = time_now_ms() + timeout_ms;

    while (TRUE) {
        // Find an available packet in the NIC
        nic_slot = find_filled_nic_slot(nic);
        if (nic_slot != NO_NIC_SLOT_AVAILABLE) {
            row = nic_slot / 64;
            offset = nic_slot % 64;

            // Copy the data to the given packet pointer
            memcpy(
                pkt,
                &nic->packets[nic_slot],
                sizeof(PACKET)
                );

            // Now, clear the reserve slot lock so the network can use that slot again
            ready_bit = InterlockedBitTestAndReset64(
                &nic->reserve_slot_lock[row],
                offset);
            ASSERT(ready_bit);
            return PACKET_RECEIVED;
        }

        // If no packets are available, we will wait for one.
        ResetEvent(nic->packets_added_to_nic);
        WaitForSingleObject(nic->packets_added_to_nic, NET_RETRY_MS);

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