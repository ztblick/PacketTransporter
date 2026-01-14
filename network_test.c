//
// Created by zachb on 1/7/2026.
//
/*
 * network_test.c
 *
 * Test harness for the network layer.
 * Validates data integrity for single-threaded and multi-threaded scenarios.
 *
 * Build:
 *   cl network_test.c network.c /Fe:network_test.exe
 *
 * Run:
 *   network_test.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

/*
 * ============================================================================
 * TEST CONFIGURATION
 * ============================================================================
 */



/*
 * ============================================================================
 * TEST UTILITIES
 * ============================================================================
 */

/*
 * fill_packet_with_pattern
 *
 * Fills a packet with a predictable pattern based on packet_id.
 * This allows validation on the receive side.
 */
static void fill_packet_with_pattern(PPACKET pkt, uint32_t packet_id, uint32_t length) {
    pkt->transmission_id = packet_id;
    pkt->length = length;
#if DEBUG
    pkt->packet_state = UNSENT;
#endif

    for (uint32_t i = 0; i < length; i++) {
        /* Pattern: each byte is (packet_id + byte_index) mod 256 */
        pkt->payload[i] = (uint8_t) packet_id;
    }
}

/*
 * validate_packet_pattern
 *
 * Checks that a received packet matches the expected pattern.
 *
 * Returns:
 *   1 if valid, 0 if corrupted
 */
static int validate_packet_pattern(PPACKET pkt) {
    uint32_t packet_id = pkt->transmission_id;

    for (uint32_t i = 0; i < pkt->length; i++) {
        uint8_t expected = (uint8_t)(packet_id);
        if (pkt->payload[i] != expected) {
            printf("  CORRUPTION: packet %u, byte %u: expected %u, got %u\n",
                   packet_id, i, expected, pkt->payload[i]);
            return 0;
        }
    }

    return 1;
}

/*
 * ============================================================================
 * SINGLE-THREADED TEST
 * ============================================================================
 *
 * Sends packets one at a time, then receives them one at a time.
 * Tests basic send/receive functionality without concurrency.
 */

static int test_single_threaded(void) {
    printf("\n");
    printf("==================================================\n");
    printf("SINGLE-THREADED TEST\n");
    printf("==================================================\n");
    printf("Sending %d packets, then receiving them.\n\n", NUM_PACKETS_SINGLE_THREADED);

    PACKET send_pkt;
    PACKET recv_pkt;
    int packets_sent = 0;
    int packets_received = 0;
    int packets_validated = 0;

    /* Send all packets */
    printf("Sending packets...\n");
    for (int i = 0; i < NUM_PACKETS_SINGLE_THREADED; i++) {
        uint32_t length = (i % MAX_PAYLOAD_SIZE) + 1;  /* Vary packet sizes */
        fill_packet_with_pattern(&send_pkt, (uint32_t)i, length);

        int result = send_packet(&send_pkt, ROLE_SENDER);
        if (result == PACKET_ACCEPTED) {
            packets_sent++;
        } else {
            printf("  FAILED to send packet %d\n", i);
        }
    }
    printf("  Sent %d packets.\n\n", packets_sent);

    /* Receive all packets */
    printf("Receiving packets...\n");
    for (int i = 0; i < packets_sent; i++) {
        int result = receive_packet(&recv_pkt, PACKET_WAIT_TIME_MS, ROLE_RECEIVER);
        if (result == PACKET_RECEIVED) {
            packets_received++;

            if (validate_packet_pattern(&recv_pkt)) {
                packets_validated++;
            }
        } else {
            printf("  TIMEOUT waiting for packet %d\n", i);
        }
    }

    /* Report results */
    printf("\n");
    printf("--------------------------------------------------\n");
    printf("RESULTS\n");
    printf("--------------------------------------------------\n");
    printf("  Packets sent:       %d\n", packets_sent);
    printf("  Packets received:   %d\n", packets_received);
    printf("  Packets validated:  %d\n", packets_validated);
    printf("\n");

    if (packets_validated == NUM_PACKETS_SINGLE_THREADED) {
        printf("  STATUS: PASS\n");
        return 1;
    } else {
        printf("  STATUS: FAIL\n");
        return 0;
    }
}

/*
 * ============================================================================
 * MULTI-THREADED TEST
 * ============================================================================
 *
 * Multiple sender threads and multiple receiver threads operate concurrently.
 * Tests synchronization of the network layer.
 */

/* Shared state for tracking received packets */
static CRITICAL_SECTION g_received_lock;
static int g_received_flags[TOTAL_PACKETS_MULTITHREADED];  /* 1 = received and valid */
static volatile LONG g_packets_received = 0;
static volatile LONG g_packets_validated = 0;

/*
 * sender_thread_func
 *
 * Each sender sends PACKETS_PER_SENDER packets with unique IDs.
 * Packet IDs are assigned based on thread index to avoid collisions.
 */
static DWORD WINAPI sender_thread_func(LPVOID param) {
    int thread_index = (int)(intptr_t)param;
    PACKET pkt;

    /* Calculate packet ID range for this thread */
    int start_id = thread_index * PACKETS_PER_SENDER;

    for (int i = 0; i < PACKETS_PER_SENDER; i++) {

        uint32_t packet_id = (uint32_t)(start_id + i);

        fill_packet_with_pattern(&pkt, packet_id, MAX_PAYLOAD_SIZE);

        int result = send_packet(&pkt, ROLE_SENDER);
        if (result != PACKET_ACCEPTED) {
            printf("  Sender %d: FAILED to send packet %u\n", thread_index, packet_id);
        }
    }

    return 0;
}

/*
 * receiver_thread_func
 *
 * Receives packets until all expected packets have been received.
 * Multiple receiver threads compete to receive packets.
 */
static DWORD WINAPI receiver_thread_func(LPVOID param) {
    int thread_index = (int)(intptr_t)param;
    PACKET pkt;

    while (g_packets_received < TOTAL_PACKETS_MULTITHREADED) {
        int result = receive_packet(&pkt, PACKET_WAIT_TIME_MS, ROLE_RECEIVER);

        if (result == PACKET_RECEIVED) {
            InterlockedIncrement(&g_packets_received);

            /* Validate packet */
            int valid = validate_packet_pattern(&pkt);
            if (valid) {
                InterlockedIncrement(&g_packets_validated);
            }

            /* Mark packet as received (for duplicate detection) */
            uint32_t packet_id = pkt.transmission_id;
            if (packet_id < TOTAL_PACKETS_MULTITHREADED) {
                EnterCriticalSection(&g_received_lock);
                if (g_received_flags[packet_id]) {
                    printf("  Receiver %d: DUPLICATE packet %u\n", thread_index, packet_id);
                }
                g_received_flags[packet_id] = 1;
                LeaveCriticalSection(&g_received_lock);
            } else {
                printf("  Receiver %d: UNEXPECTED packet ID %u\n", thread_index, packet_id);
            }
        }
        /* On timeout, loop continues and checks if we're done */
    }

    return 0;
}

static int test_multi_threaded(void) {
    printf("\n");
    printf("==================================================\n");
    printf("MULTI-THREADED TEST\n");
    printf("==================================================\n");
    printf("Sender threads:   %d\n", NUM_SENDER_THREADS);
    printf("Receiver threads: %d\n", NUM_RECEIVER_THREADS);
    printf("Packets per sender: %d\n", PACKETS_PER_SENDER);
    printf("Total packets:    %d\n\n", TOTAL_PACKETS_MULTITHREADED);

    /* Initialize shared state */
    InitializeCriticalSection(&g_received_lock);
    memset(g_received_flags, 0, sizeof(g_received_flags));
    g_packets_received = 0;
    g_packets_validated = 0;

    /* Create thread handle arrays */
    HANDLE sender_threads[NUM_SENDER_THREADS];
    HANDLE receiver_threads[NUM_RECEIVER_THREADS];

    /* Start receiver threads first (so they're ready to receive) */
    printf("Starting receiver threads...\n");
    for (int i = 0; i < NUM_RECEIVER_THREADS; i++) {
        receiver_threads[i] = CreateThread(
            NULL,                       /* default security */
            0,                          /* default stack size */
            receiver_thread_func,       /* thread function */
            (LPVOID)(intptr_t)i,        /* thread index as parameter */
            0,                          /* run immediately */
            NULL                        /* don't need thread ID */
        );

        if (receiver_threads[i] == NULL) {
            printf("  FAILED to create receiver thread %d\n", i);
            return 0;
        }
    }

    /* Start sender threads */
    printf("Starting sender threads...\n");
    for (int i = 0; i < NUM_SENDER_THREADS; i++) {
        sender_threads[i] = CreateThread(
            NULL,
            0,
            sender_thread_func,
            (LPVOID)(intptr_t)i,
            0,
            NULL
        );

        if (sender_threads[i] == NULL) {
            printf("  FAILED to create sender thread %d\n", i);
            return 0;
        }
    }

    /* Wait for all sender threads to complete */
    printf("Waiting for sender threads to complete...\n");
    WaitForMultipleObjects(NUM_SENDER_THREADS, sender_threads, TRUE, INFINITE);

    /* Close sender thread handles */
    for (int i = 0; i < NUM_SENDER_THREADS; i++) {
        CloseHandle(sender_threads[i]);
    }

    /* Wait for all receiver threads to complete */
    printf("Waiting for receiver threads to complete...\n");
    WaitForMultipleObjects(NUM_RECEIVER_THREADS, receiver_threads, TRUE, INFINITE);

    /* Close receiver thread handles */
    for (int i = 0; i < NUM_RECEIVER_THREADS; i++) {
        CloseHandle(receiver_threads[i]);
    }

    /* Check for missing packets */
    int missing_count = 0;
    for (int i = 0; i < TOTAL_PACKETS_MULTITHREADED; i++) {
        if (!g_received_flags[i]) {
            printf("  MISSING packet %d\n", i + 1);
            missing_count++;
        }
    }

    /* Cleanup */
    DeleteCriticalSection(&g_received_lock);

    /* Report results */
    printf("\n");
    printf("--------------------------------------------------\n");
    printf("RESULTS\n");
    printf("--------------------------------------------------\n");
    printf("  Packets sent:       %d\n", TOTAL_PACKETS_MULTITHREADED);
    printf("  Packets received:   %ld\n", g_packets_received);
    printf("  Packets validated:  %ld\n", g_packets_validated);
    printf("  Packets missing:    %d\n", missing_count);
    printf("\n");

    if (g_packets_validated == TOTAL_PACKETS_MULTITHREADED && missing_count == 0) {
        printf("  STATUS: PASS\n");
        return 1;
    } else {
        printf("  STATUS: FAIL\n");
        return 0;
    }
}

/*
 * ============================================================================
 * MAIN
 * ============================================================================
 */

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

    create_network_layer();

    // Initialize timing
    time_init();

    SetEvent(simulation_begin);
}

void free_all_data_and_shut_down(void) {

    SetEvent(simulation_end);

    network_cleanup();

    CloseHandle(simulation_begin);
    CloseHandle(simulation_end);
}

int main(void) {
    printf("Network Layer Test Suite\n");
    printf("========================\n");

    /* Initialize network layer */
    printf("\nAll data for network layer test...\n");

    initialize_layers_and_all_data();

    int pass_count = 0;
    int total_tests = 2;

    /* Run tests */
    if (test_single_threaded()) {
        pass_count++;
    }

    free_all_data_and_shut_down();


    if (test_multi_threaded()) {
        pass_count++;
    }

    /* Cleanup */
    printf("\nCleaning up network layer...\n");
    network_cleanup();

    /* Final summary */
    printf("\n");
    printf("==================================================\n");
    printf("SUMMARY: %d of %d tests passed\n", pass_count, total_tests);
    printf("==================================================\n");

    return (pass_count == total_tests) ? 0 : 1;
}
