#include "network.h"
#include "network_packets.h"

/**
 *  Network Layer Implementation
 *
 *  The network layer simulates the movement through the network from
 *  one machine to another. Packets enter and exit this layer following
 *  this order:
 *
 *  1. A transport-layer threads call send_packet().
 *     If a PM and sufficient data slots cannot be reserved, then the packet
 *     is rejected. Otherwise, the data is copied into the buffer and
 *     timestamped with its "arrival time."
 *
 *  2. The packets wait in the network buffer. At any point in time,
 *     it is possible for the PM to be overwritten -- if the sender
 *     sends too many packets too quickly OR if the receiver does not
 *     pull packets off quickly enough -- then a new sending thread
 *     can come in and overwrite its slot. In this case, the packet
 *     is silently dropped and its data slots are repurposed or released.
 *
 *  3. A transport-layer thread calls received_packet().
 *     If there is are no packets available in the buffer, this thread
 *     will wait for a specified amount of time. If the time expires, it
 *     will return an error code.
 *     If there ARE packets ready in the buffer, this thread will copy
 *     their data into the transport-layer packet, then free the PM and
 *     its data slots before returning a success code.
 **/


#define SLOTS_PER_LAYER 4
/**
 * A SLN represents a node on a singly-linked list of slots.
 * The list of slots is part of the PM struct, keeping track of
 * all the slots that have been allocated for the packet.
 **/
typedef struct slot_list_node {
    UINT32 number_of_slots_reserved_at_node;
    UINT32 slot_numbers[SLOTS_PER_LAYER];
    PVOID next;
} SLN, *PSLN;

/**
 * A PM struct encapsulates all the data for one packet. It has the
 * list of the slots in the buffer that contain its data, as well
 * as the total packet size. It also has a status (EMPTY, WRITING,
 * READING, READY) which also serves as a lock.
 *
 * By default, a PM keeps track of up to four slots in its array.
 * If more than 4 slots are required for a packet (i.e. if the packet
 * is larger than 4 KB) then additional slots can be chained to its
 * extra slot list.
 */
typedef struct packet_metadata {
    volatile LONG status;
    UINT32 number_of_slots_reserved;
    SLN slots;
    ULONG64 arrival_time;
} PM, *PPM;

/**
 * A Bitmap lock is used to reserve individual slots in the data buffer.
 * The bits are grabbed using interlocked bit test and set. This supports
 * lock-free concurrent access to the buffer and supports packets of
 * variable sizes.
 */
typedef struct bitmap_lock {
    ULONG64 num_bits;
    volatile PULONG64 bitmap;
} BIT_LOCK, *PBIT_LOCK;

/**
 * The network struct keeps track of all data for one of the networks
 * (sender->receiver and receiver->sender). This includes a bitmap lock,
 * a set of PMs, and a buffer of packet data.
 */
typedef struct network {
    BIT_LOCK net_lock;
    PPM metadata_slots;
    volatile PPM next_PM;
    PBYTE packet_buffer;
    HANDLE packets_present;
} NET, *PNET;

/**
 * The network state keeps track of all global variables relevant to
 * the entire network layer.
 */
typedef struct network_state {
    NET SR_net;
    NET RS_net;
    BOOL initialized;
} NET_STATE, *PNET_STATE;

// This is our global net state variable, used to track all shared data.
NET_STATE network_state;

// These are our statuses for the PMs
enum {FREE, WRITING, READY, READING};

/**
 *  Initialize the given network.
 **/
VOID net_init(PNET n) {

    ULONG64 number_of_slots = NETWORK_BUFFER_NUMBER_OF_SLOTS;

    // Initialize bitmap lock. There is one bit per 1 KB slot in the buffer.
    // So the total number of bits is equal to the number of slots, meaning the number of
    // bytes is the ceiling of this value divided by 8.
    n->net_lock.num_bits = number_of_slots;

    // All data in the bitmap is zeroed, so the initial state of each slot is unclaimed.
    n->net_lock.bitmap = zero_malloc((number_of_slots + 7) / 8);

    // Initialize PMs.
    // All data is zeroed, which sets the initial status of each packet to FREE.
    n->metadata_slots = zero_malloc(number_of_slots * sizeof(PM));

    // Initialize the buffer. Do not commit physical space until it is necessary.
    n->packet_buffer = VirtualAlloc(
                                    NULL,
                                    NETWORK_BUFFER_CAPACITY_IN_BYTES,
                                    MEM_RESERVE | MEM_COMMIT,
                                    PAGE_READWRITE
                                    );

    // Initialize the event that indicates that packets are in the buffer.
    n->packets_present = CreateEvent(
                                     NULL,                         // Default security attributes
                                     MANUAL_RESET,                 // Manual reset event!
                                     FALSE,                        // Initially the event is NOT set.
                                     TEXT("BeginSimulationEvent")  // Event name
                                    );

    // Point the next_PM to the head of the PM array.
    n->next_PM = n->metadata_slots;
}

void net_free(PNET n) {

    free(n->net_lock.bitmap);
    free(n->metadata_slots);    // TODO free all nodes in the slot lists
    VirtualFree(
                    n->packet_buffer,
            0,
        MEM_RELEASE
                );
    CloseHandle(n->packets_present);
}

/**
 * @brief Initializes the entire network layer.
 */
VOID create_network_layer(VOID) {
    // Initialize networks
    net_init(&network_state.SR_net);
    net_init(&network_state.RS_net);
    network_state.initialized = TRUE;
}

/**
 * @brief Frees network layer resources.
 **/
void free_network_layer(void) {
    network_state.initialized = FALSE;
    net_free(&network_state.SR_net);
    net_free(&network_state.RS_net);
}

/**
 * @brief Returns a PM for the incoming packet. Overwrites old PMs in the READY state, if necessary.
 *
 * @param net The network whose PMs are scanned
 * @return The PM that will be given to this packet.
 */
PPM get_next_pm(PNET net) {

    LONG status;

    // Check the next PM. If it's out of bounds, wrap
    PPM pm = net->next_PM;
    PPM next_pm;

    while (TRUE) {

        // Get the status of the PM
        status = pm->status;

        // If the PM can be claimed, we will take it!
        if (status == FREE || status == READY) {

            // Try to atomically claim this slot
            if (InterlockedCompareExchange(
                &pm->status,
                WRITING,
                status) == status)
            break;
        }

        // Move on to the next slot. If it's out of bounds, wrap.
        pm++;
        if (pm >= net->metadata_slots + NETWORK_BUFFER_NUMBER_OF_SLOTS) pm = net->metadata_slots;
    }

    // Move next pm along to the slot after this one
    next_pm = pm + 1;
    if (next_pm >= net->metadata_slots + NETWORK_BUFFER_NUMBER_OF_SLOTS) next_pm = net->metadata_slots;
    InterlockedExchangePointer((PVOID) &net->next_PM, next_pm);

    return pm;
}

/**
 * @brief Assigns the given slot to the given PM.
 * @param pm The PM to be given this slot
 * @param slot The slot to give
 */
void add_slot(PPM pm, ULONG64 slot) {
    PSLN current = &pm->slots;
    PSLN next = current->next;
    UINT32 slot_count = current->number_of_slots_reserved_at_node;

    // Traverse through the slot list, if necessary, until reaching a level with available slots.
    while (slot_count >= SLOTS_PER_LAYER) {

        if (!next) next = zero_malloc(sizeof(SLN));     // TODO chat with Landy about this.
        if (!next) ASSERT(FALSE);

        current = next;
        next = current->next;
        slot_count = current->number_of_slots_reserved_at_node;
    }

    current->slot_numbers[slot_count] = slot;
    current->number_of_slots_reserved_at_node++;
    pm->number_of_slots_reserved++;
}

/**
 * Reserve slots for the data. NOTE: if an insufficient number of slots are found (e.g. only 3 or 5),
 * these slots are NOT released. That will happen later.
 *
 * It is important to update the number of slots reserved in the PM, as this field is used
 * later on to free slots.
 *
 * @param pm The packet metadata struct into which we write the slots that are found.
 * @param slots_needed The target number of slots.
 */
void acquire_slots(PPM pm, UINT32 slots_needed, PNET net) {

    UINT32 current_slot_count = pm->number_of_slots_reserved;
    ULONG64 num_slots = net->net_lock.num_bits;
    PULONG64 bitmap_start = net->net_lock.bitmap;
    PULONG64 bitmap_end = (PULONG64) ((ULONG_PTR) bitmap_start + (num_slots + 7) / 8);
    volatile PULONG64 row = bitmap_start;
    BOOL result;

    ULONG64 slot = 0;
    ULONG64 offset = 0;
    ULONG64 mask = 0;

    while (row < bitmap_end) {

        // Update our variables
        offset = slot % 64;
        mask = (1ULL << offset);

        // Skip the whole row if it is all set
        if (*row == BITMAP_ROW_FULL_VALUE) {
            row++;
            slot += (64 - (slot % 64));
            continue;
        }

        // Skip the bit if it is set
        if (*row & mask) {
            slot++;
            if (slot % 64 == 0) row++;
            continue;
        }

        // If we can set this bit before anyone else, then we have reserved this slot.
        // We will add it to our stash of slots. And if it's the last one we need, we will return!
        result = InterlockedBitTestAndSet64((PLONG64) row, offset);
        if (!result) {
            add_slot(pm, slot);
            current_slot_count++;
            if (current_slot_count == slots_needed) return;
        }

        slot++;
        if (slot % 64 == 0) row++;
    }

    // Even if we do not get enough slots, we will return. And it's up to the caller to sort out
    // the situation where insufficient slots were allocated.
}


void release_slot(PULONG64 bitmap, UINT32 slot) {
    UINT32 row = slot / 64;
    UINT32 offset = slot % 64;
    PULONG64 bit_row = bitmap + row;
    BOOL result;

    result = InterlockedBitTestAndReset64((PLONG64) bit_row, offset);

    ASSERT(result);
}

void release_all_slots(PPM pm, PNET net) {

    PSLN current = &pm->slots;
    PSLN next = current->next;
    UINT32 slots_to_release = current->number_of_slots_reserved_at_node;
    UINT32 slot;

    while (TRUE) {
        ASSERT(slots_to_release <= SLOTS_PER_LAYER && slots_to_release > 0);
        slot = current->slot_numbers[slots_to_release - 1];
        release_slot(net->net_lock.bitmap, slot);
        slots_to_release--;

        if (slots_to_release != 0) continue;

        if (!next) break;

        current->number_of_slots_reserved_at_node = 0;
        current = next;
        next = current->next;
        slots_to_release = current->number_of_slots_reserved_at_node;
    }

    pm->number_of_slots_reserved = 0;
}


/**
 * Releases extra slots (relevant to the case where a PM is overwritten and its slots are claimed).
 * @param pm The PM containing all relevant metadata
 * @param slots_needed The total number of slots needed by this packet
 */
void release_extra_slots(PPM pm, UINT32 slots_needed, PNET net) {
    UINT32 slots_to_keep = slots_needed;
    UINT32 slots_to_release = pm->number_of_slots_reserved - slots_needed;
    UINT32 slots_checked_so_far = 0;

    PSLN current = &pm->slots;

    while (TRUE) {

        if (slots_checked_so_far + current->number_of_slots_reserved_at_node < slots_to_keep) {
            slots_checked_so_far += current->number_of_slots_reserved_at_node;
            current = current->next;
            continue;
        }

        if (slots_checked_so_far < slots_to_keep) {
            slots_checked_so_far++;
            continue;
        }

        break;
    }

    UINT32 index;

    while (TRUE) {

        if (slots_checked_so_far == slots_to_keep + slots_to_release) break;

        index = slots_checked_so_far % SLOTS_PER_LAYER;
        release_slot(net->net_lock.bitmap, current->slot_numbers[index]);

        if (index == SLOTS_PER_LAYER - 1) {
            current = current->next;
        }

        slots_checked_so_far++;
    }
}


/**
 * @brief Finds and removes a packet that has "arrived" at the end of the network.
 * @param pnet The network in which we scan for an available packet
 * @param pm The pointer back to the caller's PM. If we find a packet to remove,
 *          we will write the address of its PM here.
 * @return If a packet is found, 0. Otherwise, the closest ETA.
 */
ULONG64 try_get_packet_from_buffer(PNET pnet, PPM* pm_of_caller) {

    ULONG64 closest_eta = MAXULONG64;

    for (
        PPM pm = pnet->metadata_slots;
        pm < pnet->metadata_slots + NETWORK_BUFFER_NUMBER_OF_SLOTS;
        pm++) {

        if (pm->status != READY) continue;

        if (pm->arrival_time > time_now_ms()) {
            closest_eta = min(closest_eta, pm->arrival_time);
            continue;
        }

        if (InterlockedCompareExchange(&pm->status, READING, READY) != READY) continue;

        *pm_of_caller = pm;
        return 0;
    }

    return closest_eta;
}


/**
 * @brief Copies the data from the packet into its slots, as given by the PM.
 * @param pm The PM, containing all the slots necessary to write into.
 * @param pkt The packet, whose data needs to be added to the network.
 */
void copy_packet_data_into_slots(PPM pm, PPACKET pkt, PNET net) {

    PSLN current = &pm->slots;
    PSLN next = current->next;
    PBYTE src = (PBYTE) pkt;
    PBYTE dest;
    UINT32 slot;
    UINT32 index = 0;
    UINT32 num_copies = pm->number_of_slots_reserved;

    while (TRUE) {

        slot = current->slot_numbers[index];

        dest = net->packet_buffer + slot * NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        memcpy(dest, src, NETWORK_BUFFER_SLOT_SIZE_IN_BYTES);

        src += NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        num_copies--;

        if (num_copies == 0) break;

        index++;

        if (index == SLOTS_PER_LAYER) {
            index = 0;
            current = next;
            next = current->next;
        }
    }
}


/**
 * @brief Writes data from network slots into the given packet.
 * @param pm The PM whose slots are ready to be written out
 * @param pkt The packet, the destination for the PM's data
 */
void copy_from_slots_to_packet(PPM pm, PPACKET pkt, PNET net) {

    PSLN current = &pm->slots;
    PSLN next = current->next;
    PBYTE dest = (PBYTE) pkt;
    PBYTE src;
    UINT32 slot;
    UINT32 index = 0;
    UINT32 num_copies = pm->number_of_slots_reserved;

    while (TRUE) {

        slot = current->slot_numbers[index];

        src = net->packet_buffer + slot * NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        memcpy(dest, src, NETWORK_BUFFER_SLOT_SIZE_IN_BYTES);

        dest += NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        num_copies--;

        if (num_copies == 0) break;

        index++;

        if (index == SLOTS_PER_LAYER) {
            index = 0;
            current = next;
            next = current->next;
        }
    }
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
    PNET n;
    PPM pm;
    UINT32 slots_needed;
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
    n = &network_state.SR_net;
    if (role == ROLE_RECEIVER) n = &network_state.RS_net;

    // Determine the number of slots needed for this packet
    slots_needed = (total_packet_size_in_bytes + NETWORK_BUFFER_SLOT_SIZE_IN_BYTES - 1)
                    / NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

    // Find an available PM. This will always succeed, as it will claim the next PM, even if it is in
    // its READY state.
    pm = get_next_pm(n);
    ASSERT(pm->status == WRITING);

    // Now that we have a slot, we need to get data slots for it.
    acquire_slots(pm, slots_needed, n);

    // If there are not enough available, we will release those slots and return.
    // We will also release the PM, putting it back in its FREE state.
    if (pm->number_of_slots_reserved < slots_needed) {
        release_all_slots(pm, n);
        pm->status = FREE;
        return PACKET_REJECTED;
    }

    // When we claimed this PM, it is possible we took one in its READY state. If that was the case,
    // we already have some number of slots reserved. We need to see if it is too many, and free the extras.
    else if (pm->number_of_slots_reserved > slots_needed) {
        release_extra_slots(pm, slots_needed, n);
    }

    // Great! We have all necessary slots. Let's write our data into the memory buffer
    __try {
        copy_packet_data_into_slots(pm, pkt, n);
    }
    // If the memcpy fails, then there must be a problem with the pointer
    // passed in from the transport layer. We will reject the packet
    // and return.
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("Error copying from transport packet.\n");
        release_all_slots(pm, n);
        pm->status = FREE;
        ASSERT(FALSE);
        return PACKET_REJECTED;
    }

    // The packet has been added to the network. Now we will timestamp it with its arrival time
    // and set its status as READY.
    pm->arrival_time = time_now_ms() + LATENCY_MS;
    pm->status = READY;
    SetEvent(n->packets_present);

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
    PNET n;
    PPM pm;
    ULONG64 deadline;
    ULONG64 closest_eta = MAXULONG64;
    ULONG64 wait_time;

    // Then we determine which network state to select
    n = &network_state.SR_net;
    if (role == ROLE_SENDER) n = &network_state.RS_net;

    // Keep track of time
    deadline = time_now_ms() + timeout_ms;

    while (TRUE) {

        // Find an available packet
        closest_eta = try_get_packet_from_buffer(n, &pm);

        // If we were able to get a packet, then we will send its data up to the transport layer.
        if (closest_eta == 0) {
            ASSERT(pm->status == READING);

            __try {
                copy_from_slots_to_packet(pm, pkt, n);
            }
            // If the memcopy fails, we assume a bad actor on the transport layer,
            // And we reject the packet.
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("Error copying data to transport packet\n");
                pm->status = READY;
                ASSERT(FALSE);
                return PACKET_REJECTED;
            }

            // Great! The data was written to the packet. Let's free the data slots and move
            // the PM back into its FREE state
            release_all_slots(pm, n);
            pm->status = FREE;

            // Success! One packet was received. We can now return.
            return PACKET_RECEIVED;
        }

        // If no packets in the network, we will reset our event.
        if (closest_eta == MAXULONG64) {
            ResetEvent(n->packets_present);
        }

        // We will also set out wait time -- ideally, we will wake up JUST when the next packet has arrived.
        wait_time = min(NET_RETRY_MS, max(0, closest_eta - time_now_ms()));

        // And now we wait
        WaitForSingleObject(n->packets_present, wait_time);

        // After waking up, we check for a timeout
        if (time_now_ms() > deadline) return NO_PACKET_AVAILABLE;

        // If we don't have a timeout, then we will scan the buffer again!
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