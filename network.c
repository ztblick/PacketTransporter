/*
 * network.c
 * 
 * Network Layer Implementation
 */

#include "network.h"

/*
 * Buffer entry objects - include packet, entry time, and available time
 */

typedef struct __buffer_entry {
    ULONG64 time_available;
    PACKET packet;
} BUFFER_ENTRY, *PBUFFER_ENTRY;

/*
 * Network buffers - two directional queues with locks and condition
 * variables for signalling.
 */
typedef struct __network_buffer {
    uint16_t front;
    uint16_t back;
    uint16_t size;
    BUFFER_ENTRY entries[NETWORK_BUFFER_CAPACITY];

    // Synchronization data
    CRITICAL_SECTION lock;

    // When this buffer's condition variable is woken, a thread waiting for
    // a packet is woken.
    CONDITION_VARIABLE cv;
} NETWORK_BUFFER, *PNETWORK_BUFFER;

/*
 *  Initialize a network buffer object.
 */
void initialize_network_buffer(PNETWORK_BUFFER buffer) {
    buffer->back = 0;
    buffer->front = 0;
    buffer->size = 0;
    memset(buffer->entries, 0, NETWORK_BUFFER_CAPACITY * sizeof(BUFFER_ENTRY));

    InitializeCriticalSection(&buffer->lock);
    InitializeConditionVariable(&buffer->cv);
}

// TODO pop from buffer

/*
 *  Push the entry into the buffer.
 *  Memory must be copied into the buffer.
 *  Precondition: buffer and wire are locked, buffer has room for at least one packet.
 */
void push(PNETWORK_BUFFER buffer, PBUFFER_ENTRY entry) {

    // Copy the data into the buffer
    memcpy(buffer->entries + buffer->back, entry, sizeof(BUFFER_ENTRY));

    // Advance queue tail
    buffer->back = (buffer->back + 1) % NETWORK_BUFFER_CAPACITY;

    // Increment count
    buffer->size++;

    // Signal condition variable to wake up any waiting threads on the receiving end
    WakeConditionVariable(&buffer->cv);
}

/*
 *  Network state variable, encapsulating all information about the network.
 */
typedef struct __network_state {
    // Buffers
    NETWORK_BUFFER buffer_sender_to_receiver;
    NETWORK_BUFFER buffer_receiver_to_sender;

    // Wire locks
    CRITICAL_SECTION wire_lock_sender_to_receiver;
    CRITICAL_SECTION wire_lock_receiver_to_sender;

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
    InitializeCriticalSection(&n.wire_lock_receiver_to_sender);
    InitializeCriticalSection(&n.wire_lock_sender_to_receiver);

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
    DeleteCriticalSection(&n.buffer_receiver_to_sender.lock);
    DeleteCriticalSection(&n.buffer_sender_to_receiver.lock);
    DeleteCriticalSection(&n.wire_lock_receiver_to_sender);
    DeleteCriticalSection(&n.wire_lock_sender_to_receiver);
    n.initialized = FALSE;
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
    CRITICAL_SECTION wire_lock = n.wire_lock_sender_to_receiver;
    if (role == ROLE_RECEIVER) {
        buffer = &n.buffer_receiver_to_sender;
        wire_lock = n.wire_lock_receiver_to_sender;
    }

    // Lock wire
    EnterCriticalSection(&wire_lock);

    // Create new buffer entry based on this packet
    BUFFER_ENTRY entry;
    entry.time_available = time_now_ms() + PROPAGATION_DELAY_MS;
    memcpy(&entry.packet, pkt, sizeof(PACKET));

    // Lock buffer
    EnterCriticalSection(&buffer->lock);

    // If buffer full, return PACKET_REJECTED
    if (buffer->size == NETWORK_BUFFER_CAPACITY) {
        LeaveCriticalSection(&buffer->lock);
        LeaveCriticalSection(&wire_lock);
        return PACKET_REJECTED;
    }

    // Otherwise, push onto buffer
    push(buffer, &entry);

    // Unlock both wire and buffer
    LeaveCriticalSection(&buffer->lock);
    EnterCriticalSection(&wire_lock);

    return PACKET_ACCEPTED;  // Accepted
}


/*
 * receive_packet
 *
 * Receives a packet from the simulated network, waiting up to timeout_ms.
 */
int receive_packet(PPACKET pkt, int timeout_ms, int role) {
    if (pkt == NULL || timeout_ms < 0) {
        return -1;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return -1;
    }

    // TODO: Wait up to timeout_ms for a packet from appropriate buffer:
    //       ROLE_SENDER   → Receiver→Sender buffer
    //       ROLE_RECEIVER → Sender→Receiver buffer
    // TODO: If packet arrives, copy to pkt and return 1
    // TODO: If timeout expires, return 0

    return 0;  // Timeout (stub)
}


/*
 * try_receive_packet
 *
 * Attempts to receive a packet without waiting.
 */
int try_receive_packet(PPACKET pkt, int role) {
    if (pkt == NULL) {
        return -1;
    }

    if (role != ROLE_SENDER && role != ROLE_RECEIVER) {
        return -1;
    }

    // TODO: Check appropriate buffer for available packet:
    //       ROLE_SENDER   → Receiver→Sender buffer
    //       ROLE_RECEIVER → Sender→Receiver buffer
    // TODO: If packet available, copy to pkt and return 1
    // TODO: If no packet, return 0 immediately

    return 0;  // No packet (stub)
}