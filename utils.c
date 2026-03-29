//
// Created by zachb on 1/13/2026.
//

#include "utils.h"

#if defined(_M_ARM64)
    #define READ_COUNTER() _ReadStatusReg(ARM64_CNTVCT)
#elif defined(_M_X64) || defined(_M_IX86)
    #include <intrin.h>
    #define READ_COUNTER() __rdtsc()
#else
    #error "Unsupported architecture"
#endif

static ULONG64 tsc_start;
static ULONG64 tsc_per_ms;

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
    LARGE_INTEGER freq, qpc_begin, qpc_end;
    ULONG64 tsc_begin, tsc_end;

    QueryPerformanceFrequency(&freq);

    QueryPerformanceCounter(&qpc_begin);
    tsc_begin = READ_COUNTER();
    Sleep(50);
    QueryPerformanceCounter(&qpc_end);
    tsc_end = READ_COUNTER();

    double seconds = (double)(qpc_end.QuadPart - qpc_begin.QuadPart) / (double)freq.QuadPart;
    tsc_per_ms = (ULONG64)((double)(tsc_end - tsc_begin) / (seconds * 1000.0));

    tsc_start = READ_COUNTER();
}

ULONG64 time_now(void) {
    return READ_COUNTER() - tsc_start;
}

ULONG64 ms_to_tsc(ULONG64 ms) {
    return ms * tsc_per_ms;
}

ULONG64 tsc_to_ms(ULONG64 tsc) {
    return tsc / tsc_per_ms;
}

ULONG64 deadline_from_now_ms(ULONG64 ms) {
    return READ_COUNTER() - tsc_start + ms * tsc_per_ms;
}