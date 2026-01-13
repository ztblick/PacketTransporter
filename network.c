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
    ULONG64 lock[(NETWORK_BUFFER_CAPACITY + 63) / 64];

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
    memset(buffer->lock, 0, (NETWORK_BUFFER_CAPACITY + 63) / 64);

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
    ULONG64 reserve_slot_lock[(NIC_BUFFER_CAPACITY + 63) / 64];
    // NIC->network finds a 1 in packet_ready, writes its data to the network, then clears the
    // packet_ready bit and then the reserve_slot bit
    ULONG64 packet_ready_lock[(NIC_BUFFER_CAPACITY + 63) / 64];

    HANDLE packets_added_to_nic;

} NIC_BUFFER, *PNIC_BUFFER;

/*
 *  Net_to_NIC buffer: this buffer represents the NIC of the receiving machine. In this case, only one thread fills
 *  slots, but multiple transport threads can clear slots. So, to facilitate this, the network thread writes its
 *  data to an empty slot (represented by a 0 in the packet_ready_lock lock), then sets that bit to 1. Then, a receiver
 *  thread may come along and see a 1 in the packet ready lock. If it can successfully set that corresponding bit
 *  in the reserve_slot lock, then they know they have that packet. They transfer the data, then reset both bits to 0.
 */


/*
 *  Network state variable, encapsulating all information about the network.
 */
typedef struct __network_state {
    // Sender -> Receiver Network
    NIC_BUFFER outbound_NIC;
    NETWORK_BUFFER network_buffer;
    NIC_BUFFER inbound_NIC;

    // State
    BOOL initialized;
} NETWORK_STATE, *PNETWORK_STATE;


// Our state objects, keeping track of all relevant information for our two networks!
NETWORK_STATE SR_net;
NETWORK_STATE RS_net;

// Timing variables
LARGE_INTEGER perf_frequency;
LARGE_INTEGER time_start;

#if DEBUG
uint32_t packetStates[TOTAL_PACKETS_MULTITHREADED];
#endif

/*
 * time_init
 *
 * Initializes the high-resolution timer. Call once at program start.
 */
static void time_init(void) {
    QueryPerformanceFrequency(&perf_frequency);
    QueryPerformanceCounter(&time_start);
}

/*
 * time_now_ms
 *
 * Returns current time in milliseconds since time_init was called.
 */
static uint64_t time_now_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart - time_start.QuadPart) * 1000 / perf_frequency.QuadPart);
}

void net_init(PNETWORK_STATE n) {

    initialize_network_buffer(&n->network_buffer);

    // TODO Initialize NIC cards

    n->initialized = TRUE;

}

/*
 * network_init
 *
 * Initializes the network layer buffers and synchronization primitives.
 */
void create_network_layer(void) {

    // Initialize networks
    net_init(&SR_net);
    net_init(&RS_net);

    // Initialize timing
    time_init();

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

    // TODO free any events or other dynamically allocated data
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
void NIC_to_wire_thread(void) {

    // wait for NIC event, exit event, or timeout:
    //
    //     scan NIC for available packets
    //
    //     if none found, reset NIC event and continue
    //
    //     write packet to network buffer (if none available, expand network buffer size)
    //
    //     wait for serialization delay
    //
    //     move on to next slot
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
void wire_to_NIC_thread(void) {
    // while (running) {
    //     earliest_eta = UINT64_MAX
    //     now = time_now_ms()
    //
    //     // Scan network buffer, process arrived packets
    //     for each packet in network_buffer:
    //         if packet.arrival_time <= now:
    //             move_to_receiver_buffer(packet)
    //             remove_from_network_buffer(packet)
    //         else:
    //             if packet.arrival_time < earliest_eta:
    //                 earliest_eta = packet.arrival_time
    //
    //     // Recheck time (processing took some time)
    //     now = time_now_ms()
    //
    //     if earliest_eta <= now:
    //         // More work to do, loop immediately
    //         continue
    //
    //     if earliest_eta == UINT64_MAX:
    //         // Buffer empty, sleep until signaled that packet was added
    //         wait_for_signal(packet_added_event, INFINITE)
    //     else:
    //         // Sleep until next arrival
    //         sleep_duration = earliest_eta - now
    //         wait_for_signal(packet_added_event, sleep_duration)
    //
    //     // Either timer expired or new packet arrived - loop and scan again
    // }
}


/*
 *  Push the entry into the buffer.
 *  Memory must be copied into the buffer.
 *  Precondition: buffer and wire are locked, buffer has room for at least one packet.
 */
void push(PNETWORK_BUFFER buffer, PBUFFER_ENTRY entry) {

#if DEBUG
    ASSERT(packetStates[entry->packet.transmission_id] == UNSENT);
    ASSERT(entry->packet.packet_state == UNSENT);

    entry->packet.packet_state = SENT;
    packetStates[entry->packet.transmission_id] = SENT;
#endif

    ASSERT(buffer->size < NETWORK_BUFFER_CAPACITY);
    // Copy the data into the buffer
    memcpy(buffer->packets + buffer->back, entry, sizeof(BUFFER_ENTRY));

    // Advance queue tail
    // The other thread does not modify back, so we will not have any race
    // conditions on this value.
    buffer->back = (buffer->back + 1) % NETWORK_BUFFER_CAPACITY;

    // Increment count
    InterlockedIncrement16(&buffer->size);

    // Signal condition variable to wake up any waiting threads on the receiving end
    SetEvent(buffer->packets_added_to_network);
}

/*
 *  Pop from the buffer.
 *  If this is the last entry, reset the packets available event.
 *  Precondition: wire and buffer locks acquired, buffer is NOT empty.
 */
int pop(PNETWORK_BUFFER buffer, PPACKET destPacket) {

    ASSERT(buffer->size > 0);

    // Ensure that the arrival time of the packet is valid
    PBUFFER_ENTRY frontEntry = buffer->packets + buffer->front;
    if (frontEntry->time_available > time_now_ms()) return NO_PACKET_AVAILABLE;

#if DEBUG
    ASSERT(frontEntry->packet.packet_state == SENT);
    ASSERT(packetStates[frontEntry->packet.transmission_id] == SENT);

    frontEntry->packet.packet_state = RECEIVED;
    packetStates[frontEntry->packet.transmission_id] = RECEIVED;
#endif

    // Otherwise, copy data into the packet
    PPACKET frontPacket = &frontEntry->packet;
    destPacket->length = frontPacket->length;
    destPacket->transmission_id = frontPacket->transmission_id;
    memcpy(destPacket->payload, frontPacket->payload, frontPacket->length);

    // Advance the queue head
    buffer->front = (buffer->front + 1) % NETWORK_BUFFER_CAPACITY;

    // Decrement the size of the buffer
    InterlockedDecrement16(&buffer->size);

    // If this was the last packet available, reset the event
    if (buffer->size == 0) ResetEvent(buffer->packets_added_to_network);

    return PACKET_RECEIVED;
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

    // Select buffer based on role
    PNETWORK_BUFFER buffer = &n.buffer_sender_to_receiver;
    PCRITICAL_SECTION wire_lock = &n.wire_lock_push_sender_to_receiver;
    if (role == ROLE_RECEIVER) {
        buffer = &n.buffer_receiver_to_sender;
        wire_lock = &n.wire_lock_push_receiver_to_sender;
    }

    // Lock wire
    EnterCriticalSection(wire_lock);

    // TODO add a loop to spin, simulating synchronization delay
    //  and add back the documentation about it, including relevant calculations

    // Create new buffer entry based on this packet
    BUFFER_ENTRY entry;
    entry.time_available = time_now_ms() + PROPAGATION_DELAY_MS;
    memcpy(&entry.packet, pkt, sizeof(PACKET));

    // If buffer full, return PACKET_REJECTED
    if (buffer->size == NETWORK_BUFFER_CAPACITY) {
        LeaveCriticalSection(wire_lock);
        return PACKET_REJECTED;
    }

    // Otherwise, push onto buffer
    // This is safe, as no other producer can push at the same time,
    // and the size will be incremented during the push.
    push(buffer, &entry);

    // Unlock the wire
    LeaveCriticalSection(wire_lock);

    return PACKET_ACCEPTED;  // Accepted
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

    // Determine buffer and wire lock
    PNETWORK_BUFFER buffer = &n.buffer_sender_to_receiver;
    PCRITICAL_SECTION wire_lock = &n.wire_lock_pop_sender_to_receiver;
    if (role == ROLE_SENDER) {
        buffer = &n.buffer_receiver_to_sender;
        wire_lock = &n.wire_lock_pop_receiver_to_sender;
    }

    ULONG64 deadline = time_now_ms() + timeout_ms;

    while (TRUE) {
        // Acquire wire lock
        EnterCriticalSection(wire_lock);

        // If there are no packets available, release lock and wait for event.
        if (buffer->size == 0) {
            LeaveCriticalSection(wire_lock);
            WaitForSingleObject(buffer->packets_added_to_network, timeout_ms);

            // Check for a timeout
            if (time_now_ms() > deadline) return PACKET_REJECTED;
            continue;
        }

        // Otherwise, grab a packet from the buffer and release the lock
        ASSERT(buffer->size > 0);
        if (pop(buffer, pkt) == NO_PACKET_AVAILABLE) {
            LeaveCriticalSection(wire_lock);
            continue;
        }
        LeaveCriticalSection(wire_lock);

        ASSERT(pkt->length > 0 && pkt->length <= MAX_PAYLOAD_SIZE);
        return PACKET_RECEIVED;
    }

    return NO_PACKET_AVAILABLE;
}


/*
 * try_receive_packet
 *
 * Attempts to receive a packet without waiting.
 */
int try_receive_packet(PPACKET pkt, int role) {
    return receive_packet(pkt, 0, role);
}