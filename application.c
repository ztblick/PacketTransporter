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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "application.h"
#include "transport.h"
#include "network.h"
#include "config.h"

/*
 * time_now_ms
 *
 * Returns current time in milliseconds.
 * Uses QueryPerformanceCounter for high-resolution timing.
 */
ULONG64 time_now_ms(VOID) {
    // TODO: Implement using QueryPerformanceCounter
    return 0;
}


/*
 * sender_thread
 *
 * Thread function for sending a single transmission.
 * Records start time, calls send_transmission, reports failures.
 */
int sender_thread(PVOID transmission_info) {
    // TODO: Implement
    return 0;
}


/*
 * receiver_thread
 *
 * Thread function for receiving and validating transmissions.
 * Loops calling receive_transmission until all expected transmissions
 * are received or max_total_timeout_ms is exceeded.
 * For each received transmission, validates against sent records.
 */
int receiver_thread(VOID) {
    // TODO: Implement
    return 0;
}


/*
 * generate_test_data
 *
 * Generates random test data for a transmission.
 * Returns malloc'd buffer filled with pseudo-random bytes.
 */
static void* generate_test_data(size_t length) {
    // TODO: Implement
    return NULL;
}


/*
 * run_test
 *
 * Runs a complete test with n transmissions.
 *
 * Steps:
 *   1. Initialize stats
 *   2. Allocate and populate transmission records with test data
 *   3. Start receiver thread
 *   4. Start n sender threads
 *   5. Wait for all threads to complete
 *   6. Compute performance metrics (throughput, latency)
 *   7. Cleanup
 */
int run_test(int num_transmissions, struct test_stats* stats) {
    // TODO: Implement
    return 0;
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
void print_stats(struct test_stats* stats) {
    // TODO: Implement
}


/*
 * main
 *
 * Entry point. Initializes timer and network, runs test, prints results.
 */
int main(void) {
    // TODO: Implement
    return 0;
}


// OLD CODE BELOW

// /*
//  * application.c
//  *
//  * Application Layer Implementation
//  *
//  * Test harness for reliable data transfer. Spawns sender threads that
//  * call send_transmission() and a receiver thread that validates completed
//  * transmissions by matching transmission_id and comparing bytes.
//  *
//  * ARCHITECTURE:
//  *
//  *   ┌────────────────────────────────────────────────────────────┐
//  *   │                     APPLICATION LAYER                      │
//  *   │                                                            │
//  *   │  Sender Threads (n)              Receiver Thread (1)       │
//  *   │  ┌─────────────────┐            ┌─────────────────────┐    │
//  *   │  │ sender_thread_  │            │ receiver_thread_    │    │
//  *   │  │ func()          │            │ func()              │    │
//  *   │  │                 │            │                     │    │
//  *   │  │ - Has data and  │            │ - Loops until all   │    │
//  *   │  │   transmission_ │            │   received or       │    │
//  *   │  │   id            │            │   timeout           │    │
//  *   │  │ - Calls send_   │            │ - Calls receive_    │    │
//  *   │  │   transmission()│            │   transmission()    │    │
//  *   │  │ - May run       │            │ - Gets id, data,    │    │
//  *   │  │   concurrently  │            │   length back       │    │
//  *   │  └────────┬────────┘            │ - Validates against │    │
//  *   │           │                     │   sent records      │    │
//  *   │           │                     └──────────┬──────────┘    │
//  *   └───────────┼────────────────────────────────┼───────────────┘
//  *               │                                │
//  *               ▼                                ▼
//  *         STUDENT LAYER                   STUDENT LAYER
//  *         send_transmission()             receive_transmission()
//  *
//  * VALIDATION FLOW:
//  *
//  *   1. App layer creates transmission records with unique IDs and test data
//  *   2. Sender threads call send_transmission() with id, data, length
//  *   3. Receiver thread calls receive_transmission() repeatedly
//  *   4. For each received transmission:
//  *      a. Look up original record by transmission_id
//  *      b. Compare length
//  *      c. Compare data byte-for-byte
//  *      d. Mark record as validated or failed
//  *   5. After all received (or timeout), print statistics
//  */
//
// #include <windows.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdint.h>
// #include "application.h"
// #include "transport.h"
// #include "network.h"
// #include "config.h"
//
// /*
//  * High-resolution timer state
//  */
// static LARGE_INTEGER perf_frequency;
// static LARGE_INTEGER time_start;
//
// /*
//  * time_init
//  *
//  * Initializes the high-resolution timer. Call once at program start.
//  */
// static void time_init(void) {
//     QueryPerformanceFrequency(&perf_frequency);
//     QueryPerformanceCounter(&time_start);
// }
//
// /*
//  * time_now_ms
//  *
//  * Returns current time in milliseconds since time_init was called.
//  */
// static uint64_t time_now_ms(void) {
//     LARGE_INTEGER now;
//     QueryPerformanceCounter(&now);
//     return (uint64_t)((now.QuadPart - time_start.QuadPart) * 1000 / perf_frequency.QuadPart);
// }
//
// /*
//  * Receiver configuration - passed to receiver_thread_func
//  */
// struct receiver_config {
//     int expected_count;                     // Number of transmissions to expect
//     int per_receive_timeout_ms;             // Timeout for each receive_transmission call
//     int max_total_timeout_ms;               // Give up after this total time
//     struct transmission_record* records;    // Array of sent records for validation
//     struct test_stats* stats;               // Where to write results
// };
//
// /*
//  * Global/shared state for tracking transmissions
//  */
// static struct transmission_record* g_records = NULL;
// static int g_record_count = 0;
// static CRITICAL_SECTION g_records_cs;
//
//
// /*
//  * sender_thread
//  *
//  * Thread function for sending a single transmission.
//  */
// DWORD WINAPI sender_thread(LPVOID arg) {
//     struct transmission_record* record = (struct transmission_record*)arg;
//
//     // Record start time
//     record->send_start_ms = time_now_ms();
//
//     // Call student-implemented send_transmission
//     int result = send_transmission(record->id, record->data, record->length);
//
//     if (result != 0) {
//         // Send failed - mark record
//         // Note: validation happens on receiver side, this just tracks send failures
//         fprintf(stderr, "send_transmission failed for id %u\n", record->id);
//     }
//
//     return 0;
// }
//
//
// /*
//  * receiver_thread
//  *
//  * Thread function for receiving and validating transmissions.
//  */
// DWORD WINAPI receiver_thread(LPVOID arg) {
//     struct receiver_config* config = (struct receiver_config*)arg;
//
//     // Allocate buffer for receiving data
//     // TODO: Determine appropriate max size
//     size_t recv_buffer_size = 1024 * 1024;  // 1 MB max for now
//     void* recv_buffer = malloc(recv_buffer_size);
//     if (recv_buffer == NULL) {
//         fprintf(stderr, "Failed to allocate receive buffer\n");
//         return 1;
//     }
//
//     int received_count = 0;
//     uint64_t start_time_ms = time_now_ms();
//
//     while (received_count < config->expected_count) {
//         // Check if we've exceeded max total timeout
//         uint64_t elapsed_ms = time_now_ms() - start_time_ms;
//         if (elapsed_ms >= (uint64_t)config->max_total_timeout_ms) {
//             fprintf(stderr, "Receiver thread timed out after %lu ms\n",
//                     (unsigned long)elapsed_ms);
//             break;
//         }
//
//         uint32_t received_id;
//         size_t received_length;
//
//         // Call student-implemented receive_transmission
//         int result = receive_transmission(
//             &received_id,
//             recv_buffer,
//             &received_length,
//             config->per_receive_timeout_ms
//         );
//
//         if (result == 1) {
//             // Got a transmission - validate it
//             received_count++;
//             config->stats->transmissions_received++;
//
//             // Find matching record by transmission_id
//             struct transmission_record* match = NULL;
//             EnterCriticalSection(&g_records_cs);
//             for (int i = 0; i < g_record_count; i++) {
//                 if (g_records[i].id == received_id && g_records[i].validated == 0) {
//                     match = &g_records[i];
//                     break;
//                 }
//             }
//             LeaveCriticalSection(&g_records_cs);
//
//             if (match == NULL) {
//                 // No matching record found - unexpected transmission
//                 fprintf(stderr, "Received unexpected transmission id %u\n", received_id);
//                 config->stats->transmissions_failed++;
//             } else {
//                 // Record receive time
//                 match->receive_end_ms = time_now_ms();
//
//                 // Validate length and data
//                 int valid = 1;
//
//                 if (received_length != match->length) {
//                     fprintf(stderr, "Length mismatch for id %u: expected %zu, got %zu\n",
//                             received_id, match->length, received_length);
//                     valid = 0;
//                 } else if (memcmp(recv_buffer, match->data, received_length) != 0) {
//                     fprintf(stderr, "Data mismatch for id %u\n", received_id);
//                     valid = 0;
//                 }
//
//                 EnterCriticalSection(&g_records_cs);
//                 if (valid) {
//                     match->validated = 1;
//                     config->stats->transmissions_validated++;
//                 } else {
//                     match->validated = -1;
//                     config->stats->transmissions_failed++;
//                 }
//                 LeaveCriticalSection(&g_records_cs);
//             }
//
//         } else if (result == 0) {
//             // Timeout - no transmission ready, loop will check total timeout
//
//         } else {
//             // Error
//             fprintf(stderr, "receive_transmission returned error\n");
//             break;
//         }
//     }
//
//     free(recv_buffer);
//     return 0;
// }
//
//
// /*
//  * generate_test_data
//  *
//  * Generates random test data for a transmission.
//  */
// static void* generate_test_data(size_t length) {
//     void* data = malloc(length);
//     if (data == NULL) return NULL;
//
//     // Fill with pseudo-random data
//     unsigned char* bytes = (unsigned char*)data;
//     for (size_t i = 0; i < length; i++) {
//         bytes[i] = (unsigned char)(rand() % 256);
//     }
//
//     return data;
// }
//
//
// /*
//  * run_test
//  *
//  * Runs a complete test with n transmissions.
//  */
// int run_test(int num_transmissions, struct test_stats* stats) {
//     // Initialize stats
//     memset(stats, 0, sizeof(struct test_stats));
//     stats->transmissions_sent = num_transmissions;
//
//     // Initialize critical section
//     InitializeCriticalSection(&g_records_cs);
//
//     // Allocate transmission records
//     g_records = malloc(num_transmissions * sizeof(struct transmission_record));
//     if (g_records == NULL) {
//         DeleteCriticalSection(&g_records_cs);
//         return -1;
//     }
//     g_record_count = num_transmissions;
//
//     // Generate test data for each transmission
//     for (int i = 0; i < num_transmissions; i++) {
//         g_records[i].id = (uint32_t)(i + 1);  // IDs start at 1
//         g_records[i].length = (rand() % 10000) + 100;  // 100 to 10099 bytes
//         g_records[i].data = generate_test_data(g_records[i].length);
//         g_records[i].validated = 0;
//
//         if (g_records[i].data == NULL) {
//             // Cleanup and fail
//             for (int j = 0; j < i; j++) {
//                 free(g_records[j].data);
//             }
//             free(g_records);
//             DeleteCriticalSection(&g_records_cs);
//             return -1;
//         }
//     }
//
//     // Set up receiver config
//     struct receiver_config recv_config = {
//         .expected_count = num_transmissions,
//         .per_receive_timeout_ms = PACKET_WAIT_TIME_MS,
//         .max_total_timeout_ms = PACKET_WAIT_TIME_MS * num_transmissions * 4,
//         .records = g_records,
//         .stats = stats
//     };
//
//     // Record test start time
//     uint64_t test_start_ms = time_now_ms();
//
//     // Start receiver thread first
//     HANDLE receiver_thread_handle = CreateThread(
//         NULL, 0, receiver_thread, &recv_config, 0, NULL);
//
//     // Start sender threads
//     HANDLE* sender_handles = malloc(num_transmissions * sizeof(HANDLE));
//     for (int i = 0; i < num_transmissions; i++) {
//         sender_handles[i] = CreateThread(
//             NULL, 0, sender_thread, &g_records[i], 0, NULL);
//     }
//
//     // Wait for all sender threads to complete
//     WaitForMultipleObjects(num_transmissions, sender_handles, TRUE, INFINITE);
//     for (int i = 0; i < num_transmissions; i++) {
//         CloseHandle(sender_handles[i]);
//     }
//     free(sender_handles);
//
//     // Wait for receiver thread to complete
//     WaitForSingleObject(receiver_thread_handle, INFINITE);
//     CloseHandle(receiver_thread_handle);
//
//     // Record test end time
//     uint64_t test_end_ms = time_now_ms();
//
//     // Compute performance metrics
//     stats->total_time_ms = test_end_ms - test_start_ms;
//
//     // Calculate total bytes and latency stats
//     stats->total_bytes = 0;
//     stats->latency_min_ms = UINT64_MAX;
//     stats->latency_max_ms = 0;
//     uint64_t latency_sum_ms = 0;
//     int latency_count = 0;
//
//     for (int i = 0; i < num_transmissions; i++) {
//         stats->total_bytes += g_records[i].length;
//
//         // Only include validated transmissions in latency stats
//         if (g_records[i].validated == 1) {
//             uint64_t latency = g_records[i].receive_end_ms - g_records[i].send_start_ms;
//             latency_sum_ms += latency;
//             latency_count++;
//
//             if (latency < stats->latency_min_ms) {
//                 stats->latency_min_ms = latency;
//             }
//             if (latency > stats->latency_max_ms) {
//                 stats->latency_max_ms = latency;
//             }
//         }
//     }
//
//     // Calculate averages
//     if (latency_count > 0) {
//         stats->latency_avg_ms = (double)latency_sum_ms / latency_count;
//     } else {
//         stats->latency_avg_ms = 0;
//         stats->latency_min_ms = 0;
//     }
//
//     if (stats->total_time_ms > 0) {
//         stats->throughput_bps = (double)stats->total_bytes / ((double)stats->total_time_ms / 1000.0);
//     } else {
//         stats->throughput_bps = 0;
//     }
//
//     // Cleanup
//     for (int i = 0; i < num_transmissions; i++) {
//         free(g_records[i].data);
//     }
//     free(g_records);
//     g_records = NULL;
//     g_record_count = 0;
//     DeleteCriticalSection(&g_records_cs);
//
//     return 0;
// }
//
//
// /*
//  * print_stats
//  *
//  * Prints test statistics to stdout.
//  */
// void print_stats(struct test_stats* stats) {
//     printf("\n");
//     printf("================================================================================\n");
//     printf("                              TEST RESULTS                                      \n");
//     printf("================================================================================\n");
//     printf("\n");
//
//     // Correctness metrics
//     printf("CORRECTNESS\n");
//     printf("-----------\n");
//     printf("Transmissions sent:        %d\n", stats->transmissions_sent);
//     printf("Transmissions received:    %d (%.1f%%)\n",
//            stats->transmissions_received,
//            stats->transmissions_sent > 0
//                ? (100.0 * stats->transmissions_received / stats->transmissions_sent)
//                : 0.0);
//     printf("Transmissions validated:   %d (%.1f%% of received)\n",
//            stats->transmissions_validated,
//            stats->transmissions_received > 0
//                ? (100.0 * stats->transmissions_validated / stats->transmissions_received)
//                : 0.0);
//     printf("Transmissions failed:      %d\n", stats->transmissions_failed);
//     printf("\n");
//
//     // Performance metrics
//     printf("PERFORMANCE\n");
//     printf("-----------\n");
//     printf("Total data:                %zu bytes (%.2f KB)\n",
//            stats->total_bytes,
//            (double)stats->total_bytes / 1024.0);
//     printf("Total time:                %lu ms (%.2f sec)\n",
//            (unsigned long)stats->total_time_ms,
//            (double)stats->total_time_ms / 1000.0);
//     printf("Throughput:                %.2f bytes/sec (%.2f KB/sec)\n",
//            stats->throughput_bps,
//            stats->throughput_bps / 1024.0);
//     printf("\n");
//
//     // Latency metrics
//     printf("LATENCY\n");
//     printf("-------\n");
//     printf("Average:                   %.2f ms\n", stats->latency_avg_ms);
//     printf("Minimum:                   %lu ms\n", (unsigned long)stats->latency_min_ms);
//     printf("Maximum:                   %lu ms\n", (unsigned long)stats->latency_max_ms);
//     printf("\n");
//
//     // Overall status
//     printf("================================================================================\n");
//     if (stats->transmissions_validated == stats->transmissions_sent) {
//         printf("STATUS: ALL TRANSMISSIONS VALIDATED SUCCESSFULLY\n");
//     } else {
//         printf("STATUS: %d of %d transmissions validated\n",
//                stats->transmissions_validated, stats->transmissions_sent);
//     }
//     printf("================================================================================\n");
//     printf("\n");
// }
//
//
// /*
//  * main
//  *
//  * Entry point. Initializes network, runs test, prints results.
//  */
// int main(void) {
//     // Initialize high-resolution timer
//     time_init();
//
//     // Initialize network layer
//     if (network_init() != 0) {
//         fprintf(stderr, "Failed to initialize network\n");
//         return 1;
//     }
//
//     // Run test with a small number of transmissions
//     struct test_stats stats;
//     int num_transmissions = 3;
//
//     printf("Running test with %d transmissions...\n", num_transmissions);
//
//     if (run_test(num_transmissions, &stats) != 0) {
//         fprintf(stderr, "Test failed to run\n");
//         network_cleanup();
//         return 1;
//     }
//
//     // Print results
//     print_stats(&stats);
//
//     // Cleanup
//     network_cleanup();
//
//     return 0;
// }