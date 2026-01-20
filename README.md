# PacketTransporter

A collaborative project for learning reliable data transfer protocols by
building a transport layer from scratch.

## Overview

Students implement the **transport layer** of a network stack —
the part that takes raw, unreliable packet delivery and turns it into reliable,
ordered data transfer. Think of it as building a simplified TCP.

The application layer (test harness) and network layer (simulated unreliable channel)
are provided. The student's job is to bridge them: break data into packets,
handle losses and reordering, manage retransmissions, and reassemble everything
correctly on the other side.

## What Students Build

- **Sender**: Break transmissions into packets, send them, retransmit when needed
- **Receiver**: Reassemble packets, detect gaps,
request retransmissions, deliver complete data

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                       │
│                     (Teacher-built)                        │
│                                                            │
│   Test harness that sends data and validates correctness   │
├────────────────────────────────────────────────────────────┤
│                                                            │
│                     TRANSPORT LAYER                        │
│                    (Student-built)                         │
│                                                            │
│   send_transmission()       receive_transmission()         │
│                                                            │
│   • Packetization          • Reassembly                    │
│   • Retransmission         • Gap detection                 │
│                                                            │
├────────────────────────────────────────────────────────────┤
│                     NETWORK LAYER                          │
│                    (Teacher-built)                         │
│                                                            │
│   Simulated unreliable channel: drops, reorders, corrupts  │
└────────────────────────────────────────────────────────────┘
```

## Project Phases

**Phase 1**: Single connection

**Phase 2**: Multiple concurrent connections

**Phase 3**: Performance optimizations