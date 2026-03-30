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

#if DEBUG
typedef struct {
    ULONG64 packets_dropped_for_lack_of_slots;
    ULONG64 pms_overwritten;
} DEBUG_INFO;

DEBUG_INFO debug_info;
#endif

#define TYPICAL_SLOT_CAPACITY 4
#define NUM_PACKET_LISTS    64

#define DROP_PROBABILITY    0.01
#define DROP_RATE (int) (DROP_PROBABILITY * RAND_MAX)



/**
 * A PM struct encapsulates all the data for one packet. It has the
 * list of the slots in the buffer that contain its data, as well
 * as the total packet size.
 *
 * By default, a PM keeps track of up to four slots in its array.
 * If more than 4 slots are required for a packet (i.e. if the packet
 * is larger than 4 KB) then additional slots can be chained to its
 * extra slot list.
 */
typedef struct packet_metadata {
    SLIST_ENTRY flink;                          // NOTE: this field MUST be first.
    UINT32 number_of_slots_reserved;
    ULONG64 total_size_in_bytes;
    ULONG64 arrival_time;
    UINT32 capacity_of_slot_number_array;
    PUINT32 slot_numbers;
} PM, *PPM;

/**
 * A Bitmap lock is used to reserve individual slots in the data buffer.
 * The bits are grabbed using interlocked bit test and set. This supports
 * lock-free concurrent access to the buffer and supports packets of
 * variable sizes.
 */
typedef struct bitmap_lock {
    ULONG64 num_bits;
    volatile PLONG64 bitmap;
} BIT_LOCK, *PBIT_LOCK;

/**
 *  The timer wheel provides access to SLists of packet metadata.
 *  We use this data structure to group PMs by arrival time.
 *  Senders add them with InterlockedPushEntrySList. The particular
 *  list is determined by the hand variable. We lazily bump the hand
 *  once per millisecond by comparing the tick count to the saved timestamp.
 *
 *  NOTE: the hand grows beyond the bounds of the list array. To find the list
 *  pointed to by the hand, we must use modulo:  index = hand % NUM_PACKET_LISTS
 *
 */
typedef struct {
    SLIST_HEADER lists[NUM_PACKET_LISTS];
    volatile UINT32 hand;
    volatile ULONG64 last_updated_time;
} PM_TIMER_WHEEL, *PPM_TIMER_WHEEL;

/**
 * The network struct keeps track of all data for one of the networks
 * (sender->receiver and receiver->sender). This includes a bitmap lock,
 * a set of PMs, and a buffer of packet data.
 */
typedef struct network {
    SLIST_HEADER receiver_packet_list;
    PM_TIMER_WHEEL pm_wheel;
    BIT_LOCK net_lock;
    BIT_LOCK pm_lock;
    PPM metadata_slots;
    volatile UINT32 next_PM_slot;
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

#if DEBUG
void init_debug_info(void) {
    memset(&debug_info, 0, sizeof(DEBUG_INFO));
}
#endif
/**
 *  Initialize the given network.
 **/
VOID net_init(PNET n) {

    ULONG64 number_of_slots = NETWORK_BUFFER_NUMBER_OF_SLOTS;

    // Initialize bitmap locks. There is one bit per 1 KB slot in the buffer.
    // So the total number of bits is equal to the number of slots, meaning the number of
    // bytes is the ceiling of this value divided by 8.
    n->net_lock.num_bits = number_of_slots;
    n->pm_lock.num_bits = number_of_slots;

    // All data in the bitmap is zeroed, so the initial state of each slot is unclaimed.
    n->net_lock.bitmap = zero_malloc((number_of_slots + 7) / 8);
    n->pm_lock.bitmap = zero_malloc((number_of_slots + 7) / 8);

    // Initialize PMs.
    // All data is zeroed, which sets the initial status of each packet to FREE.
    n->metadata_slots = zero_malloc(number_of_slots * sizeof(PM));

    // Initialize the buffer. Do not commit physical space until it is necessary.
    n->packet_buffer = VirtualAlloc(
                                    NULL,
                                    NETWORK_BUFFER_CAPACITY_IN_BYTES,
                                    MEM_RESERVE,
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
    n->next_PM_slot = 0;

    // Initialize timer wheel
    for (int i = 0; i < NUM_PACKET_LISTS; i++) {
        InitializeSListHead(&n->pm_wheel.lists[i]);
    }
    n->pm_wheel.hand = 0;
    n->pm_wheel.last_updated_time = time_now();

    // Initialize receiver's packet list
    InitializeSListHead(&n->receiver_packet_list);


#if DEBUG
    init_debug_info();
#endif
}

void net_free(PNET n) {

    free(n->net_lock.bitmap);

    // Free all slot lists in the PMs
    PPM pm = n->metadata_slots;
    for (; pm < n->metadata_slots + NETWORK_BUFFER_NUMBER_OF_SLOTS; pm++) {
        if (pm->slot_numbers != NULL) free(pm->slot_numbers);
    }
    free(n->metadata_slots);

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

#if DEBUG
    printf("Packets dropped for lack of slots: %llu\n", debug_info.packets_dropped_for_lack_of_slots);
    printf("PMs overwritten due to full buffer: %llu\n", debug_info.pms_overwritten);
#endif
}

/**
 * @brief Finds an available PM.
 * @return TRUE if a PM is found (to whom address_of_next_PM will now point).
 *          FALSE if no PM is available.
 * @param net The network whose PMs are scanned
 * @return The PM that will be given to this packet.
 */
BOOL get_next_pm(PNET net, PPM* address_of_next_PM) {

    // Get a starting slot. If it's out of bounds, wrap.
    UINT32 slot = net->next_PM_slot;
    UINT32 last_slot = (slot + NETWORK_BUFFER_NUMBER_OF_SLOTS - 1) % NETWORK_BUFFER_NUMBER_OF_SLOTS;
    PLONG64 bitmap = net->pm_lock.bitmap;
    UINT32 old_slot = slot;

    while (slot != last_slot) {

        // Recalculate row and offset
        slot = slot % NETWORK_BUFFER_NUMBER_OF_SLOTS;
        UINT32 row = slot / 64;
        UINT32 offset = slot % 64;
        ULONG64 mask = (1LL << offset);

        // If the entire 64 bit chunk of the bitmap is set, advance to the next chunk.
        if (bitmap[row] == MAXULONG64) {
            old_slot = slot;
            row++;
            slot = (row * 64) % NETWORK_BUFFER_NUMBER_OF_SLOTS;

            // It may be the case that this jump goes beyond our traversal. If that happens, we will want to return.
            if (old_slot < last_slot && slot >= last_slot) return FALSE;

            continue;
        }

        // If this particular bit is set, advance.
        if (bitmap[row] & mask) {
            old_slot = slot;
            slot++;
            continue;
        }

        // Interlocked set it -- if you did not win, move on to the next bit
        if (InterlockedBitTestAndSet64(&bitmap[row], offset)) {
            old_slot = slot;
            slot++;
            continue;
        }

        // We got the PM! Now we update the address and return true
        *address_of_next_PM = net->metadata_slots + slot;
        net->next_PM_slot = slot;
        return TRUE;
    }

    return FALSE;
}

/**
 * @brief Assigns the given slot to the given PM. If the PM's slot buffer is full,
 * double its size, then add it.
 * @param pm The PM to be given this slot
 * @param slot The slot to give
 */
void add_slot(PPM pm, UINT32 slot) {

    // Check the array: if its capacity is less than the number of slots we have plus one,
    // Double its size and copy over its slots.
    if (pm->capacity_of_slot_number_array <= pm->number_of_slots_reserved) {

        // Here we add one to address the case of a size of zero.
        UINT32 new_capacity = pm->capacity_of_slot_number_array * 2 + 1;
        void* temp = realloc(pm->slot_numbers, new_capacity * sizeof(UINT32));
        if (temp == NULL) {
            ASSERT(FALSE);
            return;
        }
        pm->slot_numbers = temp;
        pm->capacity_of_slot_number_array = new_capacity;
    }
    pm->slot_numbers[pm->number_of_slots_reserved] = slot;
    pm->number_of_slots_reserved++;

    ASSERT(pm->number_of_slots_reserved <= MAX_PAYLOAD_SIZE / NETWORK_BUFFER_SLOT_SIZE_IN_BYTES + 1);
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

    if(current_slot_count >= slots_needed) return;

    ULONG64 num_slots = net->net_lock.num_bits;
    PULONG64 bitmap_start = net->net_lock.bitmap;
    PULONG64 bitmap_end = (PULONG64) ((ULONG_PTR) bitmap_start + (num_slots + 7) / 8);
    BOOL result;

    for (int i = 0; i < TIMES_TO_SCAN_BITMAP_BEFORE_EXIT; i++) {
        UINT32 slot = 0;
        ULONG64 offset = 0;
        ULONG64 mask = 0;
        volatile PULONG64 row = bitmap_start;

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

                if (current_slot_count == slots_needed) {
                    pm->number_of_slots_reserved = current_slot_count;
                    return;
                }
            }

            slot++;
            if (slot % 64 == 0) row++;
        }
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

    // Release all slots
    for (UINT32 i = 0; i < pm->number_of_slots_reserved; i++) {
        release_slot(net->net_lock.bitmap, pm->slot_numbers[i]);
    }

    pm->number_of_slots_reserved = 0;

    // Memory management: if the array is unusually large, reallocate it
    // to be a more typical size
    if (pm->capacity_of_slot_number_array <= TYPICAL_SLOT_CAPACITY) return;

    // Shrink the slot array back to baseline if it grew beyond typical use
    void* temp = realloc(pm->slot_numbers, TYPICAL_SLOT_CAPACITY * sizeof(UINT32));
    if (temp != NULL) {
        pm->slot_numbers = temp;
        pm->capacity_of_slot_number_array = TYPICAL_SLOT_CAPACITY;
    }
    // If realloc fails, we keep the oversized array — no harm done
}


/**
 * Releases extra slots (relevant to the case where a PM is overwritten and its slots are claimed).
 * @param pm The PM containing all relevant metadata
 * @param slots_needed The total number of slots needed by this packet
 */
void release_extra_slots(PPM pm, UINT32 slots_needed, PNET net) {

    if (pm->number_of_slots_reserved == slots_needed) return;

    // Double-check to ensure that the slots reserved are indeed greater than the need
    ASSERT(pm->number_of_slots_reserved > slots_needed);
    ASSERT(slots_needed > 0);
    UINT32 slots_to_release = pm->number_of_slots_reserved - slots_needed;

    // Find the location in the slot numbers from which we will remove.
    UINT32 index = pm->number_of_slots_reserved - 1;

    // Release each extra slot
    for (UINT32 i = 0; i < slots_to_release; i++) {
        release_slot(net->net_lock.bitmap, pm->slot_numbers[index]);
        index--;
    }

    pm->number_of_slots_reserved = slots_needed;
}


/**
 * @brief Copies the data from the packet into its slots, as given by the PM.
 * @param pm The PM, containing all the slots necessary to write into.
 * @param pkt The packet, whose data needs to be added to the network.
 */
void copy_packet_data_into_slots(PPM pm, PPACKET pkt, PNET net) {

    PBYTE src = (PBYTE) pkt;
    PBYTE dest;
    UINT32 slot;
    UINT32 index = 0;
    UINT32 num_copies = pm->number_of_slots_reserved;
    ULONG64 total_bytes_to_copy = pm->total_size_in_bytes;
    UINT32 bytes_to_copy_for_this_slot = 0;


    for (; index < num_copies; index++) {

        // First, we grab the next slot to find its corresponding offset in the buffer
        slot = pm->slot_numbers[index];
        dest = net->packet_buffer + slot * NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        // We assume we need to copy the whole slot. But if we don't need it all, we only
        // copy what we need.
        bytes_to_copy_for_this_slot = NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;
        if (total_bytes_to_copy < NETWORK_BUFFER_SLOT_SIZE_IN_BYTES) {
            bytes_to_copy_for_this_slot = (UINT32) total_bytes_to_copy;
        }

        // Decrement our running total by the amount we are about to copy.
        total_bytes_to_copy -= bytes_to_copy_for_this_slot;

        // We copy our data into the slot. Since the address space is reserved but not committed,
        // we may need to commit the memory now. And if that's the case, so be it!
        __try {
            memcpy(dest, src, bytes_to_copy_for_this_slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            LPVOID result = VirtualAlloc(
                                dest,
                                NETWORK_BUFFER_SLOT_SIZE_IN_BYTES,
                                MEM_COMMIT,
                                PAGE_READWRITE
                                );
            ASSERT(result);
            memcpy(dest, src, bytes_to_copy_for_this_slot);
        }

        // Advance the pointer as well as the slot index
        src += bytes_to_copy_for_this_slot;
    }
    ASSERT(total_bytes_to_copy == 0);
}


/**
 * @brief Writes data from network slots into the given packet.
 * @param pm The PM whose slots are ready to be written out
 * @param pkt The packet, the destination for the PM's data
 */
void copy_from_slots_to_packet(PPM pm, PPACKET pkt, PNET net) {

    PBYTE dest = (PBYTE) pkt;
    PBYTE src;
    UINT32 slot;
    UINT32 index = 0;
    UINT32 num_copies = pm->number_of_slots_reserved;
    ULONG64 bytes_left_to_copy = pm->total_size_in_bytes;
    UINT32 bytes_to_copy_for_this_slot = 0;

    for (; index < num_copies; index++) {

        // Find the location in the buffer corresponding with this slot
        slot = pm->slot_numbers[index];
        src = net->packet_buffer + slot * NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;

        // Calculate the size of the copy
        bytes_to_copy_for_this_slot = NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;
        if (bytes_left_to_copy < NETWORK_BUFFER_SLOT_SIZE_IN_BYTES) {
            bytes_to_copy_for_this_slot = (UINT32) bytes_left_to_copy;
        }

        // Decrement our running total by the amount we are about to copy.
        bytes_left_to_copy -= bytes_to_copy_for_this_slot;

        // We copy our data into the slot
        memcpy(dest, src, bytes_to_copy_for_this_slot);

        // Move the destination pointer along and move on to the next slot
        dest += bytes_to_copy_for_this_slot;
    }
    ASSERT(bytes_left_to_copy == 0);
}

#if DEBUG
    BOOL flag = FALSE;
    PPM over_grabbed;
#endif

/**
 *
 * @param pm The packet metadata to free
 * @param pnet The network that holds the packet metadata
 */
void free_pm(PPM pm, PNET network) {

    // Free up all data in the buffer
    release_all_slots(pm, network);

    // Get the index of the PM in the array so we know which bit in the bitmap to clear.
    UINT32 index = (UINT32)(pm - network->metadata_slots);

    // Clear the bitmap
    UINT8 result = InterlockedBitTestAndReset64(&network->pm_lock.bitmap[index / 64], index % 64);
    ASSERT(result);
}

void update_hand_if_necessary(PPM_TIMER_WHEEL pm_wheel) {
    ULONG64 old_time = pm_wheel->last_updated_time;
    ULONG64 new_time = time_now();

    // Check to see if we should advance the hand
    if (tsc_to_ms(new_time - old_time) <= 1) return;

    // If one millisecond HAS passed, then we will try updating the time
    if (InterlockedCompareExchange64(
        (volatile LONG64*) &pm_wheel->last_updated_time,
        new_time,
        old_time) != old_time) return;

    // If we won and updated the time, then it's our job to advance the hand
    InterlockedIncrement((volatile LONG*) &pm_wheel->hand);
}

/**
 * @brief Adds a packet metadata struct to the hand's SList. Advanced the hand, if necessary.
 * @param pm The packet metadata to add to a list on the timer wheel
 * @param pnet The network whose timer wheel accepts the packet metadata.
 */
void add_pm_to_list(PPM pm, PNET pnet) {
    UINT32 current_hand = pnet->pm_wheel.hand % NUM_PACKET_LISTS;
    InterlockedPushEntrySList(&pnet->pm_wheel.lists[current_hand], &pm->flink);
    update_hand_if_necessary(&pnet->pm_wheel);
}

/**
 * @brief Finds and removes a packet that has "arrived" at the end of the network.
 * @param pnet The network in which we scan for an available packet
 * @param pm The pointer back to the caller's PM. If we find a packet to remove,
 *          we will write the address of its PM here.
 * @return If a packet is found, 0. Otherwise, the closest ETA.
 *          If no packets are available, returns MAXULONG64
 */
ULONG64 try_get_available_packet(PNET pnet, PPM* pm_of_caller) {
    ULONG64 closest_eta = MAXULONG64;
    ULONG64 time = time_now();
    PPM pm;

    while (TRUE) {
        // First, check for an element on the global list.
        // If one exists and it has arrived, we will pop it from the list.
        // Then we check again: if we are the first to get this element and it has arrived,
        // we will grab it and return!

        // NOTE: we can do this cast since the flink is the first field in the PM.
        pm = (PPM) RtlFirstEntrySList(&pnet->receiver_packet_list);
        if (pm) {

            // If the packet at the head hasn't arrived yet, return its ETA.
            if (time < pm->arrival_time) return pm->arrival_time;

            // If the packet has arrived, we will try to grab it.
            pm = (PPM) InterlockedPopEntrySList(&pnet->receiver_packet_list);

            // If we can get a packet and it has arrived -- it's ours! Save its address and return.
            if (pm && time >= pm->arrival_time) {
                *pm_of_caller = pm;
                return 0;
            }

            // If we popped a valid packet, but it hasn't arrived, let's put it back on the list.
            // Then we'll return its ETA.
            if (pm) {
                closest_eta = pm->arrival_time;
                InterlockedPushEntrySList(&pnet->receiver_packet_list, &pm->flink);
                return closest_eta;
            }
        }

        // Now we know for sure there was no packet at the front of the global list.
        // It's up to us to refill it, if we can.

        // First, we will walk our way through the lists on the wheel from the index below the hand
        // and around. The last non-empty list we encounter must be the oldest. We will flush it
        // to the global list, then retry our function.
        update_hand_if_necessary(&pnet->pm_wheel);
        UINT32 hand = pnet->pm_wheel.hand % NUM_PACKET_LISTS;
        UINT32 index = (hand + NUM_PACKET_LISTS - 1) % NUM_PACKET_LISTS;

        // Check the first list. If it's null, return MAXULONG64. There are no available packets.
        if (!RtlFirstEntrySList(&pnet->pm_wheel.lists[index])) return MAXULONG64;
        UINT32 index_to_keep = index;
        index = (index + NUM_PACKET_LISTS - 1) % NUM_PACKET_LISTS;

        // Okay! We will find a list. Let's see if there's an older one.
        while (index != hand) {

            // Check out this list -- if it is empty, then we will return the previous non-null list.
            if (!RtlFirstEntrySList(&pnet->pm_wheel.lists[index])) break;

            index_to_keep = index;
            index = (index + NUM_PACKET_LISTS - 1) % NUM_PACKET_LISTS;
        }

        // Now we will flush this list to the global list
        PSLIST_ENTRY head = InterlockedFlushSList(&pnet->pm_wheel.lists[index_to_keep]);

        // If someone beat us to this list, then we know there should be packets in the global list now!
        if (!head) continue;

        // We won the list! Let's push it to the global list.
        PSLIST_ENTRY tail = head;
        ULONG count = 1;
        while (tail->Next) {
            tail = tail->Next;
            count++;
        }
        InterlockedPushListSListEx(&pnet->receiver_packet_list,
                                    head,
                                    tail,
                                    count);

        // Now we will try again!
        time = time_now();
    }
    return MAXULONG64;
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
    PNET network;
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

    // Applying dropped packets MUWAHAHAHAHA
    if (rand() < DROP_RATE) {
        return PACKET_ACCEPTED;
    }

    // Select network based on role
    network = &network_state.SR_net;
    if (role == ROLE_RECEIVER) network = &network_state.RS_net;

    // Determine the number of slots needed for this packet
    slots_needed = (UINT32) (total_packet_size_in_bytes + NETWORK_BUFFER_SLOT_SIZE_IN_BYTES - 1)
                    / NETWORK_BUFFER_SLOT_SIZE_IN_BYTES;
    ASSERT(slots_needed >= 1);

    // Find an available PM. This will always succeed, as it will claim the next PM, even if it is in
    // its READY state.
    BOOL status = get_next_pm(network, &pm);
    if (!status) return PACKET_REJECTED;
    pm->total_size_in_bytes = total_packet_size_in_bytes;

    // Now that we have a PM, we need to get data slots for it.
    acquire_slots(pm, slots_needed, network);

    // If there are not enough available, we will release those slots and return.
    // We will also release the PM, putting it back in its FREE state.
    if (pm->number_of_slots_reserved < slots_needed) {
        free_pm(pm, network);
#if DEBUG
        debug_info.packets_dropped_for_lack_of_slots++;
#endif
        return PACKET_REJECTED;
    }

    // When we claimed this PM, it is possible we took one in its READY state. If that was the case,
    // we already have some number of slots reserved. We need to see if it is too many, and free the extras.
    if (pm->number_of_slots_reserved > slots_needed) {
        release_extra_slots(pm, slots_needed, network);
#if DEBUG
        flag = TRUE;
        over_grabbed = pm;
#endif
    }

    ASSERT(pm->number_of_slots_reserved == slots_needed);
    ASSERT(slots_needed != 0);

    // Great! We have all necessary slots. Let's write our data into the memory buffer
    __try {
        copy_packet_data_into_slots(pm, pkt, network);
    }
    // If the memcpy fails, then there must be a problem with the pointer
    // passed in from the transport layer. We will reject the packet
    // and return.
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("Error copying from transport packet.\n");
        free_pm(pm, network);
        ASSERT(FALSE);
        return PACKET_REJECTED;
    }

    // The packet has been added to the network. Now we will timestamp it with its arrival time
    // and set its status as READY.
    pm->arrival_time = deadline_from_now_ms(LATENCY_MS);
    add_pm_to_list(pm, network);
    SetEvent(network->packets_present);

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
    PNET network;
    PPM pm;
    ULONG64 deadline;
    ULONG64 closest_eta = MAXULONG64;
    ULONG64 wait_time;

    // Then we determine which network state to select
    network = &network_state.SR_net;
    if (role == ROLE_SENDER) network = &network_state.RS_net;

    // Keep track of time
    deadline = deadline_from_now_ms(timeout_ms);

    while (TRUE) {

        // Find an available packet
        closest_eta = try_get_available_packet(network, &pm);

        // If we were able to get a packet, then we will send its data up to the transport layer.
        if (closest_eta == 0) {
            ASSERT(pm->total_size_in_bytes > 0);
            ASSERT(pm->number_of_slots_reserved > 0);

            __try {
                copy_from_slots_to_packet(pm, pkt, network);
            }
            // If the memcopy fails, we assume a bad actor on the transport layer,
            // And we reject the packet.
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("Error copying data to transport packet\n");
                ASSERT(FALSE);
                return PACKET_REJECTED;
            }

            // Great! The data was written to the packet. Let's free the data slots and move
            // the PM back into its FREE state
            free_pm(pm, network);

            // Success! One packet was received. We can now return.
            return PACKET_RECEIVED;
        }

        // If no packets in the network, we will reset our event.
        if (closest_eta == MAXULONG64) {
            ResetEvent(network->packets_present);
        }

        // We will also set out wait time -- ideally, we will wake up JUST when the next packet has arrived.
        wait_time = min(NET_RETRY_MS, tsc_to_ms(max(0, closest_eta - time_now())));

        // And now we wait
        WaitForSingleObject(network->packets_present, (DWORD) wait_time);

        // After waking up, we check for a timeout
        if (time_now() > deadline) return NO_PACKET_AVAILABLE;

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