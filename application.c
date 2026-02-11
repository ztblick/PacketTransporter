/*
 * application.c
 *
 * Application Layer Implementation
 *
 * Test harness for reliable data transfer. Spawns sender threads that
 * call send_transmission() and a receiver thread that validates completed
 * transmissions by matching transmission_id and comparing bytes.
 *
 * ARCHITECTURE:
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │                     APPLICATION LAYER                      │
 *   │                                                            │
 *   │  Sender Threads (n)              Receiver Thread (1)       │
 *   │  ┌─────────────────┐            ┌─────────────────────┐    │
 *   │  │ sender_thread() │            │ receiver_thread()   │    │
 *   │  │                 │            │                     │    │
 *   │  │ - Has data and  │            │ - Loops until all   │    │
 *   │  │   transmission_ │            │   received or       │    │
 *   │  │   id            │            │   timeout           │    │
 *   │  │ - Calls send_   │            │ - Calls receive_    │    │
 *   │  │   transmission()│            │   transmission()    │    │
 *   │  │ - May run       │            │ - Gets id, data,    │    │
 *   │  │   concurrently  │            │   length back       │    │
 *   │  └────────┬────────┘            │ - Validates against │    │
 *   │           │                     │   sent records      │    │
 *   │           │                     └──────────┬──────────┘    │
 *   └───────────┼────────────────────────────────┼───────────────┘
 *               │                                │
 *               ▼                                ▼
 *         TRANSPORT LAYER                 TRANSPORT LAYER
 *         send_transmission()             receive_transmission()
 *
 * VALIDATION FLOW:
 *
 *   1. App layer creates transmission records with unique IDs and test data
 *   2. Sender threads call send_transmission() with id, data, length
 *   3. Receiver thread calls receive_transmission() repeatedly
 *   4. For each received transmission:
 *      a. Look up original record by transmission_id
 *      b. Compare length
 *      c. Compare data byte-for-byte
 *      d. Mark record as validated or failed
 *   5. After all received (or timeout), print statistics
 */


#include "application.h"

// Our global variables:
APP_STATE app = {0};
STATS stats = {0};

void initialize_app_state(void) {
    app.sending_thread_count = DEFAULT_THREAD_COUNT;
    app.receiving_thread_count = DEFAULT_THREAD_COUNT;
    app.max_transmission_limit_KB = DEFAULT_TRANSMISSION_LIMIT_KB;
    app.transmission_count = DEFAULT_TRANSMISSION_COUNT;

    app.transmissions_sent = 0;
    app.transmissions_received = 0;

    memset(&app.lock_sent, 0, sizeof(ULONG64) * TRANSMISSION_LOCK_ROWS);
    memset(&app.lock_received, 0, sizeof(ULONG64) * TRANSMISSION_LOCK_ROWS);
    memset(&app.transmission_info, 0, sizeof(TRANSMISSION_INFO) * MAX_TRANSMISSION_COUNT);
}

/*
 * sender_thread
 *
 * This thread continuously sends transmissions until there are none left to send.
 */
void app_sender(void) {

    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    PTRANSMISSION_INFO transmission;
    int status;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(simulation_begin, INFINITE);

    // This comparison is not interlocked, which is okay --
    // we don't mind going around an extra time if necessary.
    while (app.transmissions_sent < app.transmission_count) {
        slot = slot % app.transmission_count;
        row = slot / 64;
        offset = slot % 64;
        mask = (1LL << offset);

        // Check the lock -- if it's totally 1s, move on to the next row
        if (app.lock_sent[row] == BITMAP_ROW_FULL_VALUE) {
            slot = (row + 1) * 64;
            continue;
        }

        // Check this bit -- if it's 1, move on to the next bit
        if (app.lock_sent[row] & mask) {
            slot++;
            continue;
        }

        // Interlocked set it -- if you did not win, move on to the next bit
        if (InterlockedBitTestAndSet64(&app.lock_sent[row], offset)) {
            slot++;
            continue;
        }

        // You won! Congrats. Now it is your job to send THIS transmission
        transmission = &app.transmission_info[slot];

        // Send it
        status = send_transmission(
            transmission->id,
            transmission->data_sent,
            transmission->bytes_sent
            );

        // If the transmission was not accepted,
        // unlock this slot and try again.
        if (status == TRANSMISSION_REJECTED) {
            InterlockedBitTestAndReset64(&app.lock_sent[row], offset);
            continue;
        }

        // Timestamp it
        ASSERT(transmission->time_sent_ms == 0);
        transmission->time_sent_ms = time_now_ms();

        // Update its status to SENT
        ASSERT(transmission->status == UNSENT);
        transmission->status = SENT;

        // Bump up the sent count
        InterlockedIncrement16(&app.transmissions_sent);
        ASSERT(app.transmissions_sent <= app.transmission_count);

        // Move on to the next slot
        slot++;
    }
}


/*
 * receiver_thread
 *
 * This thread loops, calling receive_transmission until either (A) all transmissions have been received,
 * or (B) the thread hits a timeout and exits.
 */
void app_receiver(void) {

    ULONG64 end_ms;
    LONG64 slot = 0;
    LONG64 row = 0;
    LONG64 offset = 0;
    LONG64 mask = 0;
    PTRANSMISSION_INFO info;
    int status;
    unsigned char result;

    // Wait for simulation start event
    WaitForSingleObject(simulation_begin, INFINITE);

    // Set out timeout time
    end_ms = time_now_ms() + RECEIVER_TIMEOUT_MS;

    // While there are still transmissions to receive AND we haven't timed out
    while (time_now_ms() < end_ms && app.transmissions_received < app.transmission_count) {

        // Check the lock -- if it's totally 1s, move on to the next row
        slot = slot % app.transmission_count;
        row = slot / 64;
        if (app.lock_received[row] == BITMAP_ROW_FULL_VALUE) {
            slot = (row + 1) * 64;
            continue;
        }

        // Check this bit -- if it's 1, move on to the next bit
        offset = slot % 64;
        mask = (1LL << offset);
        if (app.lock_received[row] & mask) {
            slot++;
            continue;
        }

        // Okay, let's consider THIS particular transmission
        info = &app.transmission_info[slot];

        // Acquire the lock. If you can't, then someone else beat you to it!
        if (InterlockedBitTestAndSet64(&app.lock_received[row], offset)) {
            slot++;
            continue;
        }

        // Try calling receive transmission
        status = receive_transmission(
            info->id,
            &info->data_received,
            &info->bytes_received,
            RECEIVE_TRANSMISSION_DEFAULT_TIMEOUT
            );

        // If unsuccessful, release the lock on this transmission and try again
        if (status == NO_TRANSMISSION_AVAILABLE) {
            result = InterlockedBitTestAndReset64(&app.lock_received[row], offset);
            ASSERT(result);
            continue;
        }

        ASSERT(info->id > 0 && info->id < app.transmission_count);
        ASSERT(info->bytes_received > 0 && info->bytes_received <= MAX_TRANSMISSION_LIMIT_KB * KB(1));

        // If successful -- update info!
        info->time_received_ms = time_now_ms();
        info->status = RECEIVED;

        // Increment received count for all transmissions
        InterlockedIncrement16(&app.transmissions_received);

        // Let's look for another transmission! Reset the timeout, since we were successful
        slot++;
        end_ms = time_now_ms() + RECEIVER_TIMEOUT_MS;
    }
}

void run_test(void) {
    printf("BEGINNING TEST...\n");
    printf("==================================================\n");

    // Set the simulation begin event to launch the test!
    SetEvent(simulation_begin);

    // Wait for all sender threads to complete
    printf("Waiting for sender threads to complete...\n");
    WaitForMultipleObjects(
        app.sending_thread_count,
        app.sender_threads,
        TRUE,
        INFINITE
        );

    // Wait for all receiver threads to complete
    printf("Waiting for receiver threads to complete...\n");
    WaitForMultipleObjects(
        app.sending_thread_count,
        app.receiver_threads,
        TRUE,
        INFINITE
        );

    printf("All application threads have terminated!\n");
}


/*
 * print_stats
 *
 * Prints test statistics to stdout.
 *
 * Displays:
 *   - Correctness: sent, received, validated, failed counts and percentages
 *   - Performance: total bytes, total time, throughput
 *   - Latency: average, minimum, maximum
 *   - Overall status
 */
void print_stats(void) {

    PULONG_PTR start_sent;
    PULONG_PTR end_sent;
    PULONG_PTR start_received;
    PULONG_PTR end_received;
    PTRANSMISSION_INFO info;

    stats.transmissions_sent = app.transmissions_sent;
    stats.transmissions_received = app.transmissions_received;

    // Loop through all transmissions
    for (int i = 0; i < app.transmission_count; i++) {

        info = &app.transmission_info[i];

        // If received, validate it
        if (info->status != RECEIVED) {
            stats.transmissions_missing++;
            continue;
        }

        // If validated, update validated count
        start_sent = info->data_sent;
        end_sent = (PULONG_PTR)info->data_sent + info->bytes_sent / 8;
        start_received = info->data_received;
        end_received = (PULONG_PTR)info->data_received + info->bytes_sent / 8;
        while (TRUE) {
            if (*start_sent != *start_received) {
                stats.transmissions_incomplete++;
                break;
            }
            start_sent++;
            start_received++;

            if (start_sent == end_sent &&
                start_received == end_received) {
                stats.transmissions_validated++;
                break;
            }

            if (start_sent == end_sent ||
                start_received == end_received) {
                stats.transmissions_incomplete++;
                break;
            }
        }

        // Update total data sent and total time
        stats.total_time_ms += info->time_received_ms - info->time_sent_ms;
        stats.total_bytes += info->bytes_received;
    }

    if (stats.transmissions_received > 0) {
        stats.latency_avg_ms = (double) stats.total_time_ms / stats.transmissions_received;
        stats.throughput_bps = (double) stats.total_bytes / (double) stats.total_time_ms * 1000;
    }

    // Now print things out!
    printf("TRANSMISSIONS SENT: \t\t%d\n", stats.transmissions_sent);
    printf("TRANSMISSIONS RECEIVED: \t%d\n", stats.transmissions_received);
    printf("TRANSMISSIONS MISSING: \t\t%d\n\n", stats.transmissions_missing);

    printf("TRANSMISSIONS VALIDATED: \t%d\n", stats.transmissions_validated);
    printf("TRANSMISSIONS INCOMPLETE: \t%d\n\n", stats.transmissions_incomplete);

    printf("AVERAGE LATENCY: \t\t%.2f ms\n", stats.latency_avg_ms);
    printf("THROUGHPUT: \t\t\t%.1f Kbps\n", stats.throughput_bps / KB(1));

}

void fill_transmission_with_pattern(PVOID data_in, size_t length) {
    size_t number_of_VA_stamps = length / sizeof(ULONG64);

    PULONG_PTR start = (PULONG_PTR)data_in;
    PULONG_PTR stop = start + number_of_VA_stamps;

    // Stamp each 8-byte chunk in the transmission with the VA of where it is stored
    while (start < stop) {
        *start = (ULONG_PTR)start;
        start++;
    }
}

void create_transmission_data(void) {

    TRANSMISSION_INFO temp;

    for (int i = 0; i < app.transmission_count; i++) {

        temp.bytes_sent = app.max_transmission_limit_KB * KB(1);
        temp.data_sent = zero_malloc(temp.bytes_sent);
        temp.data_received = zero_malloc(temp.bytes_sent);
        temp.id = i;
        temp.receive_count = 0;
        temp.status = UNSENT;
        temp.time_sent_ms = 0;
        temp.time_received_ms = 0;

        // Fill the transmission with its pattern
        fill_transmission_with_pattern(temp.data_sent, temp.bytes_sent);

        // Copy the newly created transmission into our array
        memcpy(
            &app.transmission_info[i],
            &temp,
            sizeof(TRANSMISSION_INFO)
            );
    }
}

void create_application_layer(void) {

    // Initialize app state as well as all transmissions
    create_transmission_data();

    // Create receiver threads
    for (int i = 0; i < app.receiving_thread_count; i++) {

        app.receiver_threads[i] = CreateThread(
            DEFAULT_SECURITY,           // default security
            DEFAULT_STACK_SIZE,         // default stack size
            (LPTHREAD_START_ROUTINE) app_receiver,
            NULL,                       // no parameter
            DEFAULT_CREATION_FLAGS,     // run immediately
            &app.receiver_thread_ids[i] // thread index is ID
        );

        if (app.receiver_threads[i] == NULL) {
            printf("  FAILED to create receiver thread %d\n", i);
            return;
        }
    }

    // Create sender threads
    for (int i = 0; i < app.sending_thread_count; i++) {

        app.sender_threads[i] = CreateThread(
            DEFAULT_SECURITY,           // default security
            DEFAULT_STACK_SIZE,         // default stack size
            (LPTHREAD_START_ROUTINE) app_sender,
            NULL,                       // no parameter
            DEFAULT_CREATION_FLAGS,     // run immediately
            &app.sender_thread_ids[i]   // thread index is ID
        );

        if (app.sender_threads[i] == NULL) {
            printf("  FAILED to create sender thread %d\n", i);
            return;
        }
    }
}

void free_application_layer(void) {

    // Close sender thread handles
    for (int i = 0; i < app.sending_thread_count; i++) {
        CloseHandle(app.sender_threads[i]);
    }

    // Close receiver thread handles
    for (int i = 0; i < app.receiving_thread_count; i++) {
        CloseHandle(app.receiver_threads[i]);
    }

}

void initialize_layers_and_all_data(void) {

    simulation_begin = CreateEvent(
        NULL,                                   // Default security attributes
        TRUE,                                   // Manual reset event!
        FALSE,                                   // Initially the event is NOT set.
        TEXT("BeginSimulationEvent")            // Event name
        );

    simulation_end = CreateEvent(
        NULL,
        TRUE,
        FALSE,
        TEXT("EndSimulationEvent")
        );

    // Initialize all layers
    create_application_layer();
    create_transport_layer();
    create_network_layer();

    // Initialize timing
    time_init();

}

void free_all_data_and_shut_down(void) {

    SetEvent(simulation_end);

    free_network_layer();
    free_transport_layer();
    free_application_layer();

    CloseHandle(simulation_begin);
    CloseHandle(simulation_end);
}

LONG64 parse_argument_as_integer(char *arg, ULONG64 min, ULONG64 max) {
    if (arg == NULL || *arg == '\0') return ARG_ERROR;   // NULL or empty
    if (*arg == '-') return ARG_ERROR;                   // Negative
    if (*arg < '0' || *arg > '9') return ARG_ERROR;      // Must start with digit (rejects whitespace)

    char *end;
    errno = 0;
    LONG64 val = strtoul(arg, &end, 10);

    if (errno == ERANGE) return ARG_ERROR;         // Overflow
    if (*end != '\0') return ARG_ERROR;            // Trailing garbage
    if (val > max) return ARG_ERROR;               // Above range
    if (val < min) return ARG_ERROR;               // Below range (handles 0)

    return val;
}

BOOL validate_input(int argc, char ** argv) {
    // If the user specifies command line arguments, ensure they are valid.
    if (argc == ARG_COUNT) {
        app.sending_thread_count = parse_argument_as_integer(argv[1],
            MIN_THREAD_COUNT,
            MAX_THREAD_COUNT);
        if (app.sending_thread_count == ARG_ERROR) {
            printf("Error: thread count must be between %d and %d.\n",
                MIN_THREAD_COUNT,
                MAX_THREAD_COUNT);
            return FALSE;
        }

        app.receiving_thread_count = parse_argument_as_integer(argv[2],
            MIN_THREAD_COUNT,
            MAX_THREAD_COUNT);
        if (app.receiving_thread_count == ARG_ERROR) {
            printf("Error: thread count must be between %d and %d.\n",
                MIN_THREAD_COUNT,
                MAX_THREAD_COUNT);
            return FALSE;
        }

        app.transmission_count = (INT16) parse_argument_as_integer(argv[3],
            MIN_TRANSMISSION_COUNT,
            MAX_TRANSMISSION_COUNT);

        if (app.transmission_count == ARG_ERROR) {
            printf("Error: transmission count must be between %d and %d.\n",
                MIN_TRANSMISSION_COUNT,
                MAX_TRANSMISSION_COUNT);
            return FALSE;
        }

        app.max_transmission_limit_KB = parse_argument_as_integer(argv[4],
            MIN_TRANSMISSION_LIMIT_KB,
            MAX_TRANSMISSION_LIMIT_KB);

        if (app.max_transmission_limit_KB == ARG_ERROR) {
            printf("Error: max transmission limit must be between %d and %d.\n",
                MIN_TRANSMISSION_LIMIT_KB,
                MAX_TRANSMISSION_LIMIT_KB);
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * main
 *
 * Entry point. Initializes timer and network, runs test, prints results.
 *
 * Usage: PacketTransporter.exe
 *      [sending threads]           number of application threads sending transmissions.    Default: 1
 *      [receiving threads]         number of app threads receiving transmissions.          Default: 1
 *      [transmission count]        number of transmissions.                                Default: 1
 *      [max transmission size]     maximum size of a transmission in the test.             Default: 128 KB
 */
int main(int argc, char** argv) {

    // Ensure proper usage
    if (argc != 1 && argc != ARG_COUNT) {
        printf("Usage: PacketTransporter.exe\n\t[sending threads]\n"
               "\t[receiving threads]\n\t[transmission count]\n\t[max transmission size]\n");
        return 1;
    }
    printf("==================================================\n");
    printf("Launching Packet Transporter\n");
    printf("==================================================\n");
    printf("Validating input...\n");

    initialize_app_state();
    if (!validate_input(argc, argv)) return 1;

    // Now that we have validated the command line arguments, let's print a message indicating that
    // we are beginning to set up the test
    printf("Input is valid!\n");
    printf("==================================================\n");
    printf("Sending threads: %llu\n", app.sending_thread_count);
    ASSERT(app.sending_thread_count > 0 && app.sending_thread_count <= MAX_THREAD_COUNT);
    printf("Receiving threads: %llu\n", app.receiving_thread_count);
    ASSERT(app.receiving_thread_count > 0 && app.receiving_thread_count <= MAX_THREAD_COUNT);
    printf("Transmission count: %d\n", app.transmission_count);
    ASSERT(app.transmission_count > 0 && app.transmission_count <= MAX_TRANSMISSION_COUNT);
    printf("Max transmission limit KB: %llu\n", app.max_transmission_limit_KB);
    ASSERT(app.max_transmission_limit_KB > 0 && app.max_transmission_limit_KB <= MAX_TRANSMISSION_LIMIT_KB);
    printf("==================================================\n");

    // Now we will initialize all layers
    printf("Initializing layers...\n");
    initialize_layers_and_all_data();
    printf("Layers initialized!\n");
    printf("==================================================\n");

    // Now we will begin the test!
    run_test();

    printf("Done!\n");
    printf("==================================================\n");

    // Finally, we will clean up and print out relevant statistics
    free_all_data_and_shut_down();

    printf("Printing statistics...\n");
    print_stats();
    printf("==================================================\n");

    return 0;
}