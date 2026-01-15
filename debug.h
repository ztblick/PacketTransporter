//
// Created by zachb on 1/9/2026.
//

#pragma once

// Global debug mode switch
#define DEBUG   1

/*
 *  Debugging tools
 */
#if DEBUG
#define ASSERT(x)       if (!(x)) {DebugBreak();}
#else
#define ASSERT(x)
#endif

#if DEBUG
#define UNSENT      0
#define SENT        1
#define RECEIVED    2
#endif

#define NUM_PACKETS_SINGLE_THREADED     256

#define NUM_SENDER_THREADS              2
#define NUM_RECEIVER_THREADS            4
#define PACKETS_PER_SENDER              256

/* Total packets in multithreaded test */
#define TOTAL_PACKETS_MULTITHREADED     (NUM_SENDER_THREADS * PACKETS_PER_SENDER)

// Somehow packets are being added multiple times. Add a check to see when
// duplicate packets are added.

#if DEBUG

extern uint32_t packetStates[TOTAL_PACKETS_MULTITHREADED];

#endif


