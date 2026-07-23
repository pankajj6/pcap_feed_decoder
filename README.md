# PCAP Feed Decoder

PCAP market data decoder for exchange feed parsing, protocol normalization, and limit order book reconstruction.

## Clone

```bash
git clone --recursive https://github.com/pankajj6/pcap_feed_decoder.git
```

If you already cloned the repository:

```bash
git submodule update --init --recursive
```

## Project Structure

```
base_lob_engine/    # Git submodule
benchmarks/
data/
├── input/
└── generated/
include/
src/
```

## Usage

Generate a PCAP from the NASDAQ binary feed:

```bash
./pcap_generator <input_binary> <output_pcap>
```

Example:

```bash
./pcap_generator \
    data/input/01302020.NASDAQ_ITCH50 \
    data/generated/01302020.pcap
```

Parse a PCAP:

```bash
./pcap_decoder <pcap_file>
```

Verbose mode:

```bash
./pcap_decoder <pcap_file> --verbose
```
