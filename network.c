/*
 * network.c
 * 
 * Network Layer Implementation
 */

#include "network.h"

/*
 * Buffer entry objects - include packet, entry time, and available time
 */

typedef struct buffer_entry {
    ULONG64 time_available;
    PACKET packet;
} BUFFER_ENTRY, *PBUFFER_ENTRY;

/*
 * Network buffers - two directional queues with locks and condition
 * variables for signalling. The buffer is protected from concurrent producers
 * or concurrent consumers by the wire lock. And with two concurrent accesses -- a producer
 * and consumer at the same time -- the size is an atomic variable preventing
 * race conditions.
 */
typedef struct network_buffer {
    uint16_t front;
    uint16_t back;
    volatile SHORT size;
    BUFFER_ENTRY entries[NETWORK_BUFFER_CAPACITY];

    // When this buffer's event is set, a thread waiting for
    // a packet is woken. This is a manual reset event, as
    // threads should continue to consume packets until there are
    // NONE left.
    HANDLE packetsAvailable;
} NETWORK_BUFFER, *PNETWORK_BUFFER;

/*
 *  Initialize a network buffer object.
 */
void initialize_network_buffer(PNETWORK_BUFFER buffer) {
    buffer->back = 0;
    buffer->front = 0;
    buffer->size = 0;
    memset(buffer->entries, 0, NETWORK_BUFFER_CAPACITY * sizeof(BUFFER_ENTRY));

    buffer->packetsAvailable = CreateEvent(
        NULL,                       // Default security attributes
        TRUE,                       // Manual reset event!
        FALSE,                      // Initially the event is NOT set.
        TEXT("PacketsReadyEvent")   // Event name
        );
}

/*
 *  Network state variable, encapsulating all information about the network.
 */
typedef struct __network_state {
    // Buffers
    NETWORK_BUFFER buffer_sender_to_receiver;
    NETWORK_BUFFER buffer_receiver_to_sender;

    // Wire locks
    CRITICAL_SECTION wire_lock_push_sender_to_receiver;
    CRITICAL_SECTION wire_lock_push_receiver_to_sender;
    CRITICAL_SECTION wire_lock_pop_sender_to_receiver;
    CRITICAL_SECTION wire_lock_pop_receiver_to_sender;

    // Timing variables
    LARGE_INTEGER perf_frequency;
    LARGE_INTEGER time_start;

    // State
    BOOL initialized;
} NETWORK_STATE, *PNETWORK_STATE;


// Our state object, keeping track of all relevant information for our network!
NETWORK_STATE n;

/*
 * time_init
 *
 * Initializes the high-resolution timer. Call once at program start.
 */
static void time_init(void) {
    QueryPerformanceFrequency(&n.perf_frequency);
    QueryPerformanceCounter(&n.time_start);
}

/*
 * time_now_ms
 *
 * Returns current time in milliseconds since time_init was called.
 */
static uint64_t time_now_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart - n.time_start.QuadPart) * 1000 / n.perf_frequency.QuadPart);
}


/*
 * network_init
 *
 * Initializes the network layer buffers and synchronization primitives.
 */
void network_init(void) {

    // Create buffers
    initialize_network_buffer(&n.buffer_receiver_to_sender);
    initialize_network_buffer(&n.buffer_sender_to_receiver);

    // Initialize locks
    InitializeCriticalSection(&n.wire_lock_push_sender_to_receiver);
    InitializeCriticalSection(&n.wire_lock_pop_sender_to_receiver);
    InitializeCriticalSection(&n.wire_lock_push_receiver_to_sender);
    InitializeCriticalSection(&n.wire_lock_pop_receiver_to_sender);

    // Initialize timing
    time_init();

    // Update state
    n.initialized = TRUE;
}


/*
 * network_cleanup
 *
 * Frees network layer resources.
 */
void network_cleanup(void) {
    DeleteCriticalSection(&n.wire_lock_push_sender_to_receiver);
    DeleteCriticalSection(&n.wire_lock_pop_sender_to_receiver);
    DeleteCriticalSection(&n.wire_lock_push_receiver_to_sender);
    DeleteCriticalSection(&n.wire_lock_pop_receiver_to_sender);
    CloseHandle(&n.buffer_receiver_to_sender.packetsAvailable);
    CloseHandle(&n.buffer_sender_to_receiver.packetsAvailable);
    n.initialized = FALSE;
}

/*
 *  Push the entry into the buffer.
 *  Memory must be copied into the buffer.
 *  Precondition: buffer and wire are locked, buffer has room for at least one packet.
 */
void push(PNETWORK_BUFFER buffer, PBUFFER_ENTRY entry) {

    ASSERT(buffer->size < NETWORK_BUFFER_CAPACITY);
    // Copy the data into the buffer
    memcpy(buffer->entries + buffer->back, entry, sizeof(BUFFER_ENTRY));

    // Advance queue tail
    // The other thread does not modify back, so we will not have any race
    // conditions on this value.
    buffer->back = (buffer->back + 1) % NETWORK_BUFFER_CAPACITY;

    // Increment count
    InterlockedIncrement16(&buffer->size);

    // Signal condition variable to wake up any waiting threads on the receiving end
    SetEvent(buffer->packetsAvailable);
}

/*
 *  Pop from the buffer.
 *  If this is the last entry, reset the packets available event.
 *  Precondition: wire and buffer locks acquired, buffer is NOT empty.
 */
int pop(PNETWORK_BUFFER buffer, PBUFFER_ENTRY entry) {

    ASSERT(buffer->size > 0);

    // Ensure that the arrival time of the packet is valid
    PBUFFER_ENTRY frontEntry = buffer->entries + buffer->front;
    if (frontEntry->time_available > time_now_ms()) return NO_PACKET_AVAILABLE;

    // Otherwise, copy data into the entry
    memcpy(entry, buffer->entries + buffer->front, sizeof(BUFFER_ENTRY));

    // Advance the queue head
    buffer->front = (buffer->front + 1) % NETWORK_BUFFER_CAPACITY;

    // Decrement the size of the buffer
    InterlockedDecrement16(&buffer->size);

    // If this was the last packet available, reset the event
    if (buffer->size == 0) ResetEvent(buffer->packetsAvailable);

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
    CRITICAL_SECTION wire_lock = n.wire_lock_push_sender_to_receiver;
    if (role == ROLE_RECEIVER) {
        buffer = &n.buffer_receiver_to_sender;
        wire_lock = n.wire_lock_push_receiver_to_sender;
    }

    // Lock wire
    EnterCriticalSection(&wire_lock);

    // TODO add a loop to spin, simulating synchronization delay
    //  and add back the documentation about it, including relevant calculations

    // Create new buffer entry based on this packet
    BUFFER_ENTRY entry;
    entry.time_available = time_now_ms() + PROPAGATION_DELAY_MS;
    memcpy(&entry.packet, pkt, sizeof(PACKET));

    // If buffer full, return PACKET_REJECTED
    if (buffer->size == NETWORK_BUFFER_CAPACITY) {
        LeaveCriticalSection(&wire_lock);
        return PACKET_REJECTED;
    }

    // Otherwise, push onto buffer
    // This is safe, as no other producer can push at the same time,
    // and the size will be incremented during the push.
    push(buffer, &entry);

    // Unlock the wire
    EnterCriticalSection(&wire_lock);

    return PACKET_ACCEPTED;  // Accepted
}


/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
int receive_packet(PPACKET pkt, ULONG64 timeout_ms, int role) {
    if (pkt == NULL || timeout_ms < 0) {
        return NO_PACKET_AVAILABLE;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return NO_PACKET_AVAILABLE;
    }

    // Determine buffer and wire lock
    PNETWORK_BUFFER buffer = &n.buffer_sender_to_receiver;
    CRITICAL_SECTION wire_lock = n.wire_lock_pop_sender_to_receiver;
    if (role == ROLE_SENDER) {
        buffer = &n.buffer_receiver_to_sender;
        wire_lock = n.wire_lock_pop_receiver_to_sender;
    }

    ULONG64 remaining_time = timeout_ms;
    ULONG64 deadline = time_now_ms() + remaining_time;

    while (TRUE) {
        // Acquire wire lock
        EnterCriticalSection(&wire_lock);

        // If there are no packets available, release lock and wait for event.
        if (buffer->size == 0) {
            LeaveCriticalSection(&wire_lock);
            WaitForSingleObject(buffer->packetsAvailable, remaining_time);

            // Check for a timeout
            remaining_time = deadline - time_now_ms();
            if (remaining_time < 0) return PACKET_REJECTED;
            continue;
        }

        // Otherwise, grab a packet from the buffer and release the lock
        BUFFER_ENTRY entry;
        pop(buffer, &entry);
        LeaveCriticalSection(&wire_lock);

        // fill pkt and return success
        memcpy(&pkt, &entry.packet, sizeof(PACKET));
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