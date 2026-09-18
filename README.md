# PCAP Feed Decoder

A high-performance PCAP decoder for **NASDAQ TotalView-ITCH 5.0**, with support for low-latency ITCH message decoding and Level-3 limit order book reconstruction.

The decoder processes packet captures containing Ethernet, IPv4, UDP, MoldUDP64 and NASDAQ ITCH 5.0 data. It can be used either to benchmark the packet/message decoding path independently or to reconstruct Level-3 order books across the symbols present in the feed.

The repository also includes a PCAP generator for creating packet captures from the publicly available NASDAQ ITCH binary files. The generator is optional: if a suitable ITCH PCAP is already available, it can be passed directly to `pcap_decoder`.

---

## Table of Contents

- [Why this Project](#why-this-project)
- [Architecture](#architecture)
- [Performance](#performance)
- [PCAP Generator](#pcap-generator)
  - [Why a PCAP Generator?](#why-a-pcap-generator)
  - [Packet Size Model](#packet-size-model)
- [Decode Modes](#decode-modes)
- [Clone](#clone)
- [Usage](#usage)
- [Repository Structure](#repository-structure)
- [Features](#features)
- [Related Projects](#related-projects)

---

## Why this Project

NASDAQ TotalView-ITCH 5.0 market data is commonly available as historical ITCH binary files, while packet-oriented applications work with the network packets in which those messages are transmitted.

This project provides a packet-level processing path for NASDAQ ITCH 5.0 PCAP data. It is intended to be useful both for benchmarking packet/message processing and for working with real market-data captures when they are available.

The decoder currently provides:

* PCAP packet and MoldUDP64 processing
* NASDAQ TotalView-ITCH 5.0 message decoding
* field extraction and byte-order conversion
* MoldUDP64 session and sequence-number handling
* Level-3 limit order book reconstruction
* separate benchmarking of message decoding and full book reconstruction
* per-stock benchmarking through a compile-time stock-locate filter

The decoder can be used directly with an existing PCAP capture. The included `pcap_generator` is an optional utility for cases where a packet capture is not available.

NASDAQ's public historical ITCH data is distributed as length-prefixed ITCH binary streams rather than the original packet captures. The generator uses those real historical ITCH messages to construct PCAP files with the network framing required by the decoder. This makes it possible to benchmark and test packet-level processing using publicly available data without requiring access to a proprietary packet capture.

The current implementation focuses on deterministic decoding and Level-3 reconstruction. The packet-decoding layer is also intended to provide a foundation for separating raw feed processing from downstream market-data processing as the project develops. This can include producing clean decoded ITCH data for independent research and feature-extraction pipelines rather than requiring all downstream work to be coupled directly to packet-to-book reconstruction.

---

# Architecture

```mermaid
flowchart TD

    A["NASDAQ ITCH Binary"]
        --> B["pcap_generator"]

    B --> C["PCAP"]

    C --> D["pcap_decoder"]

    D --> E["Packet / MoldUDP64 Processing"]
    E --> F["ITCH 5.0 Decoding"]

    F --> G{"Decode / Benchmark Path"}

    G --> H["Parser Benchmark"]
    G --> I["Direct Reconstruction"]
    G --> J["MarketState Reconstruction"]

    H --> K["Decoded ITCH Message"]

    I --> L["BaseLOBEngine"]
    J --> M["Event"]
    M --> N["reconstruct_market_state()"]
    N --> L

    L --> O["Level-3 Limit Order Book"]
```

The decoder processes the PCAP in several layers:

1. **PCAP packet processing**
   Reads the packet records and extracts the captured network frame.

2. **Network and MoldUDP64 processing**
   Parses the Ethernet/IP/UDP/MoldUDP64 framing and tracks the MoldUDP64 session and packet sequence number.

3. **ITCH 5.0 decoding**
   Extracts the length-prefixed ITCH messages and decodes their fields, including the required byte-order conversions.

4. **Optional downstream processing**
   Decoded messages can either stop at the parser/decoding layer for benchmarking, or be passed into the Level-3 reconstruction path.

The decoder also contains sequence handling for packets that arrive out of order. Packets that are ahead of the expected sequence number are temporarily held in a bounded buffer and processed when the missing sequence becomes available. Packets that arrive too late, or cannot be retained within the configured buffering window, are counted as out-of-order drops.

The included `pcap_generator` is a separate input-generation utility. It is useful when the original packet capture is unavailable, but it is not required by the decoder.

---

# Performance

The benchmarks below separate the cost of ITCH packet/message decoding from the cost of maintaining the full Level-3 order book.

The benchmark PCAP was generated from the publicly available NASDAQ TotalView-ITCH binary data using `pcap_generator`. The current generated capture is approximately **30 GB** and contains **15.9 million packets** and **202.6 million ITCH messages**. The generator was stopped after approximately 15.9 million packets when the available disk space was exhausted. The resulting capture is still large enough to provide a useful workload for benchmarking the decoder.

All-symbol benchmarks process the feed across the symbols contained in the capture. The single-stock benchmarks use the same PCAP and still traverse the complete packet stream.

## Parser / ITCH Decoding

The `MEASURE_PARSER_LATENCY` compile-time flag isolates the packet and ITCH decoding path.

With this flag enabled, the decoder:

- reads and processes the PCAP packets;
- processes the Ethernet/IP/UDP/MoldUDP64 framing;
- extracts the ITCH messages;
- decodes the ITCH message fields;
- performs the required byte-order conversions;
- processes messages across all symbols in the feed;
- does **not** update the Level-3 order book.

This therefore measures the cost of parsing and decoding the ITCH feed without the additional work of maintaining the order book.

### All-symbol result

**~99.9 million ITCH messages/sec**  
**P50: 10 ns · P95: 30 ns · P99: 40 ns**

| Metric | Result |
|---|---:|
| Packets/sec | ~7.87 million |
| ITCH messages/sec | **~99.9 million** |
| P50 | **10 ns** |
| P95 | **30 ns** |
| P99 | **40 ns** |
| Total packets | 10,000,000 |
| Total ITCH messages | 126,999,530 |

A second run produced approximately **90.5 million ITCH messages/sec**, with the same 10 ns P50, 30 ns P95 and 40 ns P99 latency profile.

These results are for the **all-symbol parser path**. They should not be compared directly with the reconstruction throughput below, since the reconstruction benchmark performs substantially more work.

## Single-Stock Parser Benchmark

`SPECIFIC_STOCK_LOCATE` provides a compile-time benchmark mode for isolating one ticker.

For example:

```bash
-DSPECIFIC_STOCK_LOCATE=398
````

When this flag is enabled, the decoder still traverses the complete PCAP and processes the packet and message structure normally, but after decoding the ITCH stock-locate field it skips messages whose stock locate does not match the selected value.

For unrelated messages, the decoder therefore does not continue into the remaining message fields or perform the downstream processing. The selected stock's messages go through the normal decoding path.

This flag is intended for **benchmarking a particular ticker**, not for identifying tickers in general. Stock-locate values are feed-specific identifiers and are not universally mapped to the same ticker. For the benchmark capture used here, stock locate `398` was identified as **AMZN** before running the benchmark.

### AMZN result

**~16.3k ITCH messages/sec**   
**P50: 10 ns · P95: 31 ns · P99: 41 ns**

| Metric                     |      Result |
| -------------------------- | ----------: |
| Packets/sec                |    ~965,653 |
| Selected ITCH messages/sec | **~16,305** |
| Total packets traversed    |  15,911,917 |
| Selected ITCH messages     |     268,673 |
| P50                        |   **10 ns** |
| P95                        |   **31 ns** |
| P99                        |   **41 ns** |

The packet rate here refers to the **entire PCAP traversal**. Only 268,673 messages belong to the selected stock, so the selected-message throughput should not be interpreted as the throughput of a smaller single-stock PCAP.

## Full Level-3 Reconstruction

`MEASURE_RECON_LATENCY` measures the complete reconstruction path.

In this mode, the decoder performs the packet processing and ITCH decoding described above and then applies the decoded messages to the Level-3 order books.

The benchmark reconstructs the order-level book state across the symbols contained in the feed, including additions, executions, cancels, deletes and replaces. The reconstructed books are maintained by `BaseLOBEngine`.

This additional order-book work is the main reason the throughput is substantially lower than the parser-only result.

### All-symbol result

**~4.3 million ITCH messages/sec**   
**P50: 170 ns · P95: 383 ns · P99: 631 ns**

| Metric              |            Result |
| ------------------- | ----------------: |
| Packets/sec         |          ~337,112 |
| ITCH messages/sec   | **~4.29 million** |
| Total packets       |        15,911,917 |
| Total ITCH messages |       202,608,852 |
| P50                 |        **170 ns** |
| P95                 |        **383 ns** |
| P99                 |        **631 ns** |

A separate run produced approximately **4.27 million ITCH messages/sec**, with P50/P95/P99 of 171/391/641 ns.

The benchmark feed contains thousands of active NASDAQ symbols, and the reconstruction path maintains the corresponding order-level books rather than simply decoding the messages and discarding them.

## Single-Stock Level-3 Reconstruction

The same `SPECIFIC_STOCK_LOCATE` mechanism can be combined with `MEASURE_RECON_LATENCY` to measure the reconstruction cost for a single ticker while still traversing the complete PCAP.

For the AMZN benchmark using stock locate `398`:

**~16.7k ITCH messages/sec**  
**P50: 10 ns · P95: 40 ns · P99: 50 ns**

| Metric                     |      Result |
| -------------------------- | ----------: |
| Packets/sec                |    ~989,181 |
| Selected ITCH messages/sec | **~16,702** |
| Total packets traversed    |  15,911,917 |
| Selected ITCH messages     |     268,673 |
| P50                        |   **10 ns** |
| P95                        |   **40 ns** |
| P99                        |   **50 ns** |

This isolates the reconstruction workload for the selected ticker without removing the cost of traversing the complete packet capture.

---

# PCAP Generator

## Why a PCAP Generator?

NASDAQ publicly distributes historical TotalView-ITCH data as length-prefixed ITCH binary files rather than the original network packet captures.

These files contain the actual historical ITCH messages, but not the Ethernet, IPv4, UDP and MoldUDP64 framing that surrounds those messages when the feed is transmitted over the network.

`pcap_generator` reconstructs this missing framing and writes the result as a PCAP file that can be consumed directly by `pcap_decoder`.

The generated PCAP contains:

* Ethernet headers
* IPv4 headers
* UDP headers
* MoldUDP64 headers
* the original length-prefixed NASDAQ ITCH messages

The ITCH messages are taken directly from the publicly available NASDAQ historical data. The packet boundaries and network headers are constructed by the generator.

The resulting file is therefore **synthetic at the packet-framing level, not at the market-data level**. It provides a reproducible way to test and benchmark packet-oriented software using real historical ITCH messages without requiring access to an original packet capture.

If an actual NASDAQ ITCH PCAP is already available, the generator is not required. The capture can be passed directly to `pcap_decoder`.

---

## Packet Size Model

The initial version of the generator packed ITCH messages into each packet until the 1500-byte IP MTU was reached. This produces valid PCAP data, but results in packets that are predominantly close to the maximum size.

The current generator instead varies the packet-size boundary using a **three-state Markov process**. The purpose is to introduce variation and temporal clustering in packet sizes rather than treating every packet independently or filling every packet to the MTU.

The three states are:

| State     |      Packet size | Target message density |
| --------- | ---------------: | ---------------------- |
| **Quiet** | 114 or 166 bytes | 1–2 messages           |
| **Mid**   |    218–582 bytes | 3–10 messages          |
| **Burst** |       1514 bytes | Up to the MTU          |

The packet boundaries are derived from the protocol structure. The generated frame reserves 62 bytes for the Ethernet, IPv4, UDP and MoldUDP64 headers. A maximum-size ITCH message occupies 50 bytes, and its 2-byte length prefix makes the maximum message record 52 bytes. This gives the 114-byte one-message boundary and the subsequent 52-byte increments used by the generator. 

### State distribution

The target long-run state distribution is:

* **60% Quiet**
* **20% Mid**
* **20% Burst**

The choice of a non-uniform packet-size distribution is motivated by published measurements of market-data packet captures. In particular, Keysight's analysis of market-data feeds shows that packet-size distributions vary by exchange and that averages alone do not capture the range of packet sizes present in real feeds. ([Keysight][1])

The transition probabilities used by the generator are:

| From / To | Quiet |  Mid | Burst |
| --------- | ----: | ---: | ----: |
| **Quiet** |  0.80 | 0.18 |  0.02 |
| **Mid**   |  0.56 | 0.28 |  0.16 |
| **Burst** |  0.04 | 0.18 |  0.78 |

These values are not independent random weights chosen for each packet. They are the transition probabilities of the Markov process required to produce the target long-run state distribution while retaining persistence between states.

The transition matrix was derived from the target state frequencies using the stationary-distribution relationship of the Markov chain, with state persistence chosen to produce clustered periods of similar packet sizes. The calculation and derivation are documented in the project's design notes and will be described more fully if the packet-generation methodology is published separately.

The implementation represents the probabilities using a 16-bit integer scale and uses a lightweight WyRand-based generator for the state transition and packet-size selection.

### Packet-size regimes

The three states correspond to different packet-size regimes:

**Quiet**

The generator selects either 114 or 166 bytes, corresponding to packets containing approximately one or two maximum-size ITCH message records.

**Mid**

The generator selects packet boundaries from 218 through 582 bytes in 52-byte increments, corresponding to approximately three through ten maximum-size message records.

**Burst**

The packet boundary is fixed at 1514 bytes, corresponding to a 1500-byte IP MTU plus the 14-byte Ethernet header.

The generator then packs actual ITCH records from the source binary until the selected packet boundary is reached or the next message would exceed it.

### Current scope

The current generator models **packet-size variation and clustering**. It does not attempt to reproduce the exact packet boundaries of a historical exchange capture.

It also does not currently generate packet loss or out-of-order packet sequences. The decoder already has bounded sequence-number buffering for handling packets that arrive out of order. A future generator extension can deliberately introduce gaps or reorder packets to provide controlled test cases for that decoder functionality.

---

# Decode Modes

The decoder provides two Level-3 reconstruction paths built on the same underlying `BaseLOBEngine`.

| Mode            | Description                                                                                                                                                                                              |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Direct**      | Decodes each ITCH message and applies it directly through the engine's `itch_*` interfaces.                                                                                                              |
| **MarketState** | Converts decoded ITCH messages into the internal `Event` representation and replays them through `reconstruct_market_state()`. This path also supports maintaining `LobState` derived market statistics. |

Both reconstruction modes operate on the same decoded ITCH feed and reconstruct the Level-3 order book.

### Parser-only benchmark

The decoder also has a separate parser benchmark path enabled with `MEASURE_PARSER_LATENCY`.

When this macro is defined, the ITCH messages are still read and decoded, including the field extraction and byte-order conversion, but the Level-3 book update is skipped.

This allows the packet/message decoding path to be measured independently from the substantially more expensive order-book reconstruction.

The corresponding `MEASURE_RECON_LATENCY` macro instead measures the decoding & reconstruction path with the Level-3 book updates enabled.

### Single-stock benchmarking

`SPECIFIC_STOCK_LOCATE` provides a compile-time way to isolate processing for one stock locate when benchmarking.

For example:

```bash
-DSPECIFIC_STOCK_LOCATE=398
```

causes messages whose stock locate is not `398` to be skipped after the stock locate has been decoded. The PCAP is still traversed normally, so the benchmark continues to read and inspect the complete packet stream.

This is intended for **per-ticker benchmarking**, not as a replacement for the full-feed decoder.

Stock locate numbers are feed-specific identifiers and are not self-explanatory. For the specific itch file used in this repository, stock locate `398` corresponds to **AMZN**. A user working with another capture should determine the relevant stock-locate mapping for that feed rather than assuming the number identifies the same ticker universally.

---

# Clone

Clone the repository together with the `base_lob_engine` submodule.

```bash
git clone --recursive https://github.com/pankajj6/pcap_feed_decoder.git
cd pcap_feed_decoder
````

If the repository has already been cloned without its submodules:

```bash
git submodule update --init --recursive
```

---

# Repository Structure

```text
pcap_feed_decoder/
├── base_lob_engine/       Git submodule
├── data/
│   ├── input/             NASDAQ ITCH binary input
│   └── generated/         Generated PCAP files
├── include/
│   ├── itch_packet_processor.h
│   └── nasdaq_itch50.h
├── src/
│   ├── pcap_decoder.cpp
│   └── pcap_generator.cpp
├── .gitignore
├── .gitmodules
└── README.md
```

The `base_lob_engine` directory is maintained as a separate Git repository and is included here as a submodule.

---

# Usage

The project does not require a build system. The decoder and generator can be compiled directly with a C++23 compiler.

## Build the Decoder

```bash
g++ -std=c++23 -O3 \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o pcap_decoder
```

The resulting executable accepts a PCAP file:

```bash
./pcap_decoder <pcap_file>
```

For example:

```bash
./pcap_decoder data/generated/01302020.pcap
```

### Verbose Output

Use `--verbose` to print decoded sample messages while processing the capture:

```bash
./pcap_decoder data/generated/01302020.pcap --verbose
```

### MarketState Reconstruction

The default reconstruction path applies decoded ITCH messages directly through the engine's `itch_*` interfaces.

To use the `MarketState` path instead:

```bash
./pcap_decoder data/generated/01302020.pcap --market-state
```

The two options can also be combined:

```bash
./pcap_decoder data/generated/01302020.pcap \
    --market-state \
    --verbose
```

---

## Parser Benchmark

Compile with `MEASURE_PARSER_LATENCY` to benchmark packet and ITCH message decoding without Level-3 order book updates:

```bash
g++ -std=c++23 -O3 \
    -DMEASURE_PARSER_LATENCY \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o parser_bench
```

Run it with:

```bash
./parser_bench data/generated/01302020.pcap
```

This processes the complete packet stream and decodes the ITCH messages across all symbols while excluding the order-book update path.

---

## Full Reconstruction Benchmark

Compile with `MEASURE_RECON_LATENCY` to benchmark the complete packet-to-Level-3 reconstruction path:

```bash
g++ -std=c++23 -O3 \
    -DMEASURE_RECON_LATENCY \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o recon_bench
```

Run:

```bash
./recon_bench data/generated/01302020.pcap
```

This includes ITCH decoding and the corresponding Level-3 order book updates.

---

## Single-Stock Benchmark

`SPECIFIC_STOCK_LOCATE` can be combined with either benchmark mode to isolate one stock.

For example, the benchmark in this repository uses stock locate `398`, which was identified as AMZN in the benchmark capture:

```bash
g++ -std=c++23 -O3 \
    -DMEASURE_PARSER_LATENCY \
    -DSPECIFIC_STOCK_LOCATE=398 \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o parser_bench
```

Run:

```bash
./parser_bench data/generated/01302020.pcap
```

For single-stock Level-3 reconstruction:

```bash
g++ -std=c++23 -O3 \
    -DMEASURE_RECON_LATENCY \
    -DSPECIFIC_STOCK_LOCATE=398 \
    -Ibase_lob_engine \
    -Iinclude \
    src/pcap_decoder.cpp \
    -o recon_bench
```

Run:

```bash
./recon_bench data/generated/01302020.pcap
```

The stock-locate value is feed-specific. `398` is not a universal identifier for AMZN; it is the value observed for AMZN in the benchmark capture used here.

---

## Generate a PCAP

`pcap_generator` is an optional utility for converting the publicly available length-prefixed NASDAQ ITCH binary data into a PCAP with reconstructed network framing.

Build it with:

```bash
g++ -std=c++23 -O3 \
    -Iinclude \
    src/pcap_generator.cpp \
    -o pcap_generator
```

Usage:

```bash
./pcap_generator <input_binary> <output_pcap> [max_packets]
```

Example:

```bash
./pcap_generator \
    data/input/01302020.NASDAQ_ITCH50 \
    data/generated/01302020.pcap
```

To limit the generated capture to a specific number of packets:

```bash
./pcap_generator \
    data/input/01302020.NASDAQ_ITCH50 \
    data/generated/01302020.pcap \
    1000000
```

If a suitable NASDAQ ITCH PCAP is already available, `pcap_generator` is not required. The PCAP can be passed directly to `pcap_decoder`.

---

# Features

* NASDAQ TotalView-ITCH 5.0 message decoding
* Ethernet, IPv4, UDP and MoldUDP64 packet processing
* MoldUDP64 session and sequence-number handling
* Bounded out-of-order packet buffering
* Level-3 limit order book reconstruction
* Direct reconstruction through `itch_*` interfaces
* Event-based reconstruction through `reconstruct_market_state()`
* `LobState` support for derived market-state statistics
* Separate parser and reconstruction benchmarks
* Compile-time single-stock benchmarking with `SPECIFIC_STOCK_LOCATE`
* PCAP generation from public NASDAQ ITCH binary data
* Optional verbose decoded-message output

---

# Related Projects

- [**Base LOB Engine**](https://github.com/pankajj6/base_lob_engine): The underlying C++ limit order book and matching engine used by this decoder for Level-3 reconstruction.
- [**TALON**](https://github.com/pankajj6/talon): A deterministic, latency-aware agent-based market simulator that utilizes the same `base_lob_engine` for discrete-event exchange matching.