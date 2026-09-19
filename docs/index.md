---
layout: default
title: PCAP Feed Decoder
subtitle: Packet-level decoding and Level-3 reconstruction infrastructure for NASDAQ TotalView-ITCH 5.0.
---

## Overview

**PCAP Feed Decoder** is a market-data processing system for working from captured network traffic to decoded exchange messages and reconstructed Level-3 order-book state.

The current implementation targets **NASDAQ TotalView-ITCH 5.0** and covers Ethernet, IPv4, UDP, MoldUDP64 session and sequence handling, ITCH message decoding, packet-level benchmarking, and Level-3 reconstruction.

The project is maintained as part of **SMMG Research** work on market microstructure, market-data infrastructure, and deterministic simulation.

[View the repository on GitHub](https://github.com/pankajj6/pcap_feed_decoder) · [Visit SMMG Research](https://smmg-research.vercel.app/)

## System overview

![PCAP Feed Decoder processing architecture]({{ '/assets/architecture.svg' | relative_url }})

The processing path is organized into four layers:

1. **Packet processing** — reads PCAP records and extracts captured Ethernet frames.
2. **Transport and session processing** — handles Ethernet/IP/UDP/MoldUDP64 framing and sequence state.
3. **Protocol decoding** — parses length-prefixed ITCH 5.0 messages and performs field conversion.
4. **Market-state processing** — applies decoded events to a Level-3 order book when reconstruction is enabled.

This separation allows packet and message decoding to be benchmarked independently from order-book state mutation.

## Performance

Benchmarks were run against a **15 million packet PCAP (~7 GB)** containing **190,907,472 ITCH messages** across all active symbols.

Latency is measured once per packet, from the beginning of packet processing through completion of that packet. The reported percentiles therefore describe packet-processing latency.

### Complete-feed ITCH decoding

| Metric | Result |
| --- | --- |
| Feed | NASDAQ TotalView-ITCH 5.0 |
| Packets | **15,000,000** |
| Capture size | **~7 GB** |
| ITCH messages | **190,907,472** |
| ITCH throughput | **~96.0M messages/s** |
| Packet throughput | **~7.54M packets/s** |
| P50 | **40 ns** |
| P95 | **291 ns** |
| P99 | **451 ns** |

This is the parser-only path. The complete feed is decoded without invoking Level-3 book updates.

### Single-instrument Level-3 reconstruction

For a selected instrument, the packet stream is traversed while only the target instrument's decoded messages are passed to the reconstruction path.

For this benchmark capture, stock locate `398` corresponds to **AMZN**.

| Metric | Result |
| --- | --- |
| Packets traversed | **15,000,000** |
| Selected ITCH messages | **254,926** |
| Selected-message throughput | **~187k messages/s** |
| Packet throughput | **~11.01M packets/s** |
| P50 | **20 ns** |
| P95 | **91 ns** |
| P99 | **652 ns** |

This measurement represents the reconstruction path for one active instrument.

### Full-feed reconstruction

A separate workload reconstructs Level-3 books across the complete set of active symbols in the capture.

| Metric | Result |
| --- | --- |
| Packets | **15,000,000** |
| ITCH messages | **190,907,472** |
| ITCH throughput | **~5.59M messages/s** |
| P50 | **592 ns** |
| P95 | **9.06 µs** |
| P99 | **11.25 µs** |

This workload measures the cost of maintaining many order-level books while consuming the complete feed.

## Reconstruction paths

The decoder provides two Level-3 paths over the same `BaseLOBEngine`:

| Path | Operation |
| --- | --- |
| **Direct** | Decoded ITCH messages are applied directly through the engine's `itch_*` interfaces. |
| **MarketState** | Decoded messages are converted into the internal `Event` representation and replayed through `reconstruct_market_state()`. |

The MarketState path can additionally maintain `LobState` fields for derived market-state statistics.

The parser benchmark forms a separate measurement boundary, allowing packet and message decoding to be evaluated without invoking either reconstruction path.

## PCAP generation

Public historical NASDAQ TotalView-ITCH data is distributed as length-prefixed binary messages rather than original network captures. A packet decoder, however, needs the surrounding network framing.

The project therefore includes a PCAP generator that places the historical ITCH messages inside Ethernet, IPv4, UDP, and MoldUDP64 framing so they can be processed by the packet path.

The benchmark data retains the original historical ITCH records. The generated PCAP supplies the missing packetization layer.

### Packet generation model

The generator uses a **three-state Markov process** to model packet-size regimes:

| State | Packet boundary | Approx. records |
| --- | --- | --- |
| **Quiet** | 114 / 166 bytes | 1–2 |
| **Mid** | 218–582 bytes | 3–10 |
| **Burst** | 1514 bytes | up to MTU |

The target long-run state distribution is **60% Quiet / 20% Mid / 20% Burst**.

The transition matrix is:

| From → To | Quiet | Mid | Burst |
| --- | ---: | ---: | ---: |
| **Quiet** | 0.80 | 0.18 | 0.02 |
| **Mid** | 0.56 | 0.28 | 0.16 |
| **Burst** | 0.04 | 0.18 | 0.78 |

The matrix gives packet sizes temporal persistence: quiet periods tend to remain quiet, while burst periods can persist across consecutive packets. Transition selection is implemented on a 16-bit integer scale using a lightweight WyRand-based generator.

The packet-size boundaries follow the framing available to the generator. Ethernet, IPv4, UDP, and MoldUDP64 contribute **62 bytes** of network overhead. Each MoldUDP64 record carries a 2-byte length prefix, and the maximum ITCH message payload is 50 bytes, giving a 52-byte maximum record size.

> The generated PCAP is a reproducible packet-processing workload built from real historical ITCH messages. The generator models the missing packetization layer rather than reproducing the exact packet boundaries of a historical exchange capture.

The generator design considered continuous random sizing, shuffled lookup tables, precomputed Markov sequences, and asynchronous buffering before settling on the inline Markov formulation. The resulting implementation remains single-threaded and avoids a finite-sequence boundary in the generated packet stream.

## Decoder architecture

### Packet processing

The decoder reads PCAP records and processes the captured Ethernet frame. The packet layer handles capture traversal independently of market-state updates.

### MoldUDP64 sequencing

The decoder tracks MoldUDP64 session and sequence information. Packets arriving ahead of the expected sequence are retained in a bounded buffer and processed when the missing sequence becomes available. Packets that arrive too late, or cannot be retained within the configured window, are counted as out-of-order drops.

### ITCH 5.0 decoding

The protocol layer extracts the length-prefixed ITCH records, decodes message fields, and performs the required byte-order conversions.

The current protocol implementation is **NASDAQ TotalView-ITCH 5.0**.

## Reproducibility

The project is intentionally build-system-light. The decoder and generator can be compiled directly with a C++23 compiler.

```bash
git clone --recursive https://github.com/pankajj6/pcap_feed_decoder.git
cd pcap_feed_decoder
```

### Parser benchmark

```bash
g++ -std=c++23 -O3 \
    -DMEASURE_PARSER_LATENCY \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o parser_bench

./parser_bench data/generated/01302020.pcap
```

### Single-stock reconstruction

```bash
g++ -std=c++23 -O2 \
    -DMEASURE_RECON_LATENCY \
    -DSPECIFIC_STOCK_LOCATE=398 \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o recon_bench

./recon_bench data/generated/01302020.pcap
```

### PCAP generation

```bash
g++ -std=c++23 -O3 \
    -Iinclude \
    src/pcap_generator.cpp \
    -o pcap_generator

./pcap_generator \
    data/input/01302020.NASDAQ_ITCH50 \
    data/generated/01302020.pcap
```

## Scope

The current implementation covers:

- NASDAQ TotalView-ITCH 5.0
- Ethernet / IPv4 / UDP / MoldUDP64 processing
- MoldUDP64 session and sequence handling
- bounded out-of-order buffering
- ITCH message decoding
- Level-3 order-book reconstruction
- packet-level latency instrumentation
- reproducible PCAP generation

The current generator models packetization for reproducible benchmarks. It does not reproduce the exact packet boundaries of a historical capture and does not currently inject packet loss or out-of-order sequences into generated traffic.

## Research direction

The current implementation carries the feed through the complete path to Level-3 reconstruction, providing an end-to-end representation of the market state produced from the captured feed.

The longer-term data path is to make the decoder the market-data ingestion layer: remove network and transport framing, decode the exchange messages, and write clean binary message streams for downstream research systems.

Those downstream systems can then perform instrument-specific reconstruction, feature extraction, and deeper market-microstructure analysis without repeating packet traversal and protocol decoding.

This separates network-facing ingestion from the research workloads built on top of the decoded event stream.

## Related infrastructure

### BaseLOBEngine

A reusable C++ limit-order-book and matching-engine core used by the reconstruction and simulation systems.

[Repository](https://github.com/pankajj6/base_lob_engine)

### TALON

A deterministic, latency-aware agent-based market simulator built around discrete-event exchange processing and the same underlying LOB engine.

[Repository](https://github.com/pankajj6/talon)

---

**SMMG Research** · Market microstructure · market-data infrastructure · deterministic simulation
