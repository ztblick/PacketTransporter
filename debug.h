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

