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

#include "network.h"

/*
 * Transmission record - tracks a sent transmission for later validation
 */
struct transmission_record {
    uint32_t id;
    void* data;
    size_t length;
    int validated;          // 0 = pending, 1 = success, -1 = failure
    uint64_t send_start_ms; // Timestamp when send_transmission called
    uint64_t receive_end_ms;// Timestamp when receive_transmission returned
};

/*
 * Test statistics - populated after test completes
 */
struct test_stats {
    // Correctness metrics
    int transmissions_sent;
    int transmissions_received;
    int transmissions_validated;  // Byte-for-byte match
    int transmissions_failed;     // Mismatch or missing

    // Performance metrics
    size_t total_bytes;           // Total bytes across all transmissions
    uint64_t total_time_ms;       // Wall clock time from first send to last receive
    double throughput_bps;        // Bytes per second

    // Latency metrics (in milliseconds)
    double latency_avg_ms;
    uint64_t latency_min_ms;
    uint64_t latency_max_ms;
};

/*
 * sender_thread
 *
 * Thread function for sending a single transmission.
 * Multiple instances can run concurrently on separate threads.
 *
 * Parameters:
 *   arg - Pointer to a transmission_info record containing:
 *         id, data, and length to send
 *
 * Returns:
 *   0 - Transmission sent and acknowledged by transport layer
 *   1 - Error
 *
 * Usage:
 *   CreateThread(NULL, 0, sender_thread, &transmission_info, 0, NULL);
 */
int sender_thread(PVOID transmission_info);

/*
 * receiver_thread
 *
 * Thread function for receiving and validating transmissions.
 * Runs on a single thread, repeatedly calls receive_transmission
 * until all expected transmissions are received or timeout.
 *
 * Parameters:
 *   none
 *
 * Returns:
 *   0 (results stored in test_stats)
 *
 * Usage:
 *   CreateThread(NULL, 0, receiver_thread, &config, 0, NULL);
 */
int receiver_thread(VOID);

/*
 * run_test
 *
 * Runs a complete test with n transmissions.
 *
 * Parameters:
 *   num_transmissions - Number of transmissions to send
 *   stats             - Pointer to struct where results will be written
 *
 * Returns:
 *   0  - Test completed (check stats for pass/fail details)
 *  -1  - Test setup failed
 */
int run_test(int num_transmissions, struct test_stats* stats);

/*
 * print_stats
 *
 * Prints test statistics to stdout.
 *
 * Parameters:
 *   stats - Pointer to populated test_stats struct
 */
void print_stats(struct test_stats* stats);