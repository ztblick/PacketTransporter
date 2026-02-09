/*
 * application.h
 * 
 * Application Layer API
 * 
 * This layer provides the test harness for the student-implemented
 * reliable data transfer. It spawns sender threads that call send_transmission
 * and a receiver thread that validates completed transmissions.
 */

#pragma once

#include "transport.h"

#define ARG_COUNT   5
#define ARG_ERROR   -1

#define DEFAULT_THREAD_COUNT            1
#define MIN_THREAD_COUNT                1
#define MAX_THREAD_COUNT                64

#define DEFAULT_TRANSMISSION_LIMIT_KB   256
#define MIN_TRANSMISSION_LIMIT_KB       1
#define MAX_TRANSMISSION_LIMIT_KB       (1024 * 1024)

#define DEFAULT_TRANSMISSION_COUNT      1
#define MIN_TRANSMISSION_COUNT          1
#define MAX_TRANSMISSION_COUNT          64
#define TRANSMISSION_LOCK_ROWS          ((MAX_TRANSMISSION_COUNT + 63) / 64)

#define STATUS_UNSENT       0
#define STATUS_SENT         1
#define STATUS_RECEIVED     2

// Assuming a 256 KB transmission going one packet at a time, we would expect this to take
// size / packet size * roundtrip time = 256 * 20 ms = ~5000 ms = 5 s
#define RECEIVER_TIMEOUT_MS                     5000
#define RECEIVE_TRANSMISSION_DEFAULT_TIMEOUT    50

#define BITMAP_ROW_FULL_VALUE       (~0ULL)

typedef struct transmission_info {
    PVOID data_sent;
    PVOID data_received;
    UINT32 id;
    UINT32 status;
    LONG64 receive_count;
    size_t bytes_sent;
    size_t bytes_received;
    ULONG64 time_sent_ms;
    ULONG64 time_received_ms;
} APP_TRANSMISSION_INFO, *PAPP_TRANSMISSION_INFO;

// The lock_sent field is used to protect against multiple sending threads sending
// the same transmission.

// The lock_received field should not be necessary for receiving threads, though,
// because the receiving threads will have the ID of the incoming transmission.
// Which means they will only access THEIR slot in info_buffer. That said,
// if a transmission is duplicated concurrently, this lock can be used to check
// for a duplicate transmission.
typedef struct app_state {
    LONG64 sending_thread_count;
    LONG64 receiving_thread_count;
    LONG64 max_transmission_limit_KB;

    INT16 transmission_count;
    INT16 transmissions_sent;
    INT16 transmissions_received;

    HANDLE sender_threads[MAX_THREAD_COUNT];
    HANDLE receiver_threads[MAX_THREAD_COUNT];
    ULONG sender_thread_ids[MAX_THREAD_COUNT];
    ULONG receiver_thread_ids[MAX_THREAD_COUNT];

    LONG64 lock_sent[TRANSMISSION_LOCK_ROWS];
    LONG64 lock_received[TRANSMISSION_LOCK_ROWS];

    APP_TRANSMISSION_INFO transmission_info[MAX_TRANSMISSION_COUNT];
} APP_STATE, *PAPP_STATE;

/*
 * Test statistics - populated after test completes
 */
typedef struct test_stats {
    // Correctness metrics
    int transmissions_sent;
    int transmissions_received;
    int transmissions_validated;
    int transmissions_missing;
    int transmissions_incomplete;

    // Performance metrics
    size_t total_bytes;           // Total bytes across all transmissions
    ULONG64 total_time_ms;        // Wall clock time from first send to last receive
    double throughput_bps;        // Bits per second

    // Latency metrics (in milliseconds)
    double latency_avg_ms;
} STATS, *PSTATS;

/*
 * sender_thread
 *
 * Thread function for sending a single transmission.
 * Multiple instances can run concurrently on separate threads.
 */
void app_sender(void);

/*
 * receiver_thread
 *
 * Thread function for receiving and validating transmissions.
 * Runs on a single thread, repeatedly calls receive_transmission
 * until all expected transmissions are received or timeout.
 */
void app_receiver(void);

/*
 * run_test
 *
 * Runs a complete test.
 */
void run_test(void);

/*
 * print_stats
 *
 * Prints test statistics to stdout.
 */
void print_stats(void);