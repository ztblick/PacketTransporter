//
// Created by zachb on 1/13/2026.
//

#include "utils.h"

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
    return (ULONG64)((now.QuadPart - time_start.QuadPart) * 1000 / perf_frequency.QuadPart);
}