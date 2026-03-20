//
// Created by zachb on 1/13/2026.
//

#include "utils.h"

// Global variable definitions (declared extern in utils.h)
LARGE_INTEGER perf_frequency;
LARGE_INTEGER time_start;
HANDLE simulation_begin;
HANDLE simulation_end;

PVOID zero_malloc(size_t bytes_to_allocate) {
    PULONG_PTR destination = malloc(bytes_to_allocate);
    ASSERT(destination);
    memset(destination, 0, bytes_to_allocate);
    return destination;
}

/*
 * time_init
 *
 * Initializes the high-resolution timer. Call once at program start.
 */
void time_init(void) {
    QueryPerformanceFrequency(&perf_frequency);
    QueryPerformanceCounter(&time_start);
}

/*
 * time_now_ms
 *
 * Returns current time in milliseconds since time_init was called.
 */
ULONG64 time_now_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Guard: threads may call this before time_init() runs
    if (perf_frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&perf_frequency);
        time_start = now;
        return 0;
    }

    return (ULONG64)((now.QuadPart - time_start.QuadPart) * 1000 / perf_frequency.QuadPart);
}