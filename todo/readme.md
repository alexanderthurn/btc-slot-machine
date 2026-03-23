# Bitcoin Slot Machine - High-Performance Filter Pipeline

This C++ and PHP toolset processes terabytes of raw Bitcoin data (`blk*.dat`), extracts 20-byte Hash160 address payloads, and intelligently builds a **multi-tiered 64-bit Bloom Filter Hierarchy (32 MB to 2048 MB)** for instantaneous, 0-RAM lookups on any conventional Web Server.

## The Unified Command-Line Tool

### Core Features
1. **Frontend (`index.html`)**: 
   - Supports Legacy, SegWit, and Taproot addresses.
   - Offline decoding and Hash160 calculation via JavaScript.
   - Persistent "Recent Checks" history with mempool links.
   - Performance metrics for both client and server processing.

2. **C++ Preprocessing (`todo/main.cpp`)**:
   - Parallelized parsing using all available CPU cores via `std::thread`.
   - Thread-safe Bloom filter updates using `std::atomic<uint64_t>`.
   - All 7 filter arrays (~4 GB total) kept in RAM for the entire run — loaded once at startup, saved after each chunk as a checkpoint.
   - Graceful termination (SIGINT/SIGTERM): finishes the current file and saves progress before exiting.
   - Incremental updates: re-running automatically skips fully completed chunks and only re-parses the last partial chunk (which may have grown since the last run) plus any new chunks.
   - Extracts 20-byte Hash160 payloads directly from Bitcoin `.dat` files (P2PKH, P2SH, P2WPKH).
   - Fully dynamic multi-tiered filter generation (32MB up to 2GB).
   - O(1) direct-to-disk verification for testing addresses without loading massive filters into RAM.

3. **Backend API (`index.php`)**:
   - Zero-RAM footprint: Queries filter files directly from disk via byte offsets.
   - Ultra-low latency: Typically < 1ms processing time.

## Installation & Setup

### Prerequisites
- Docker (recommended) OR
- C++17 Compiler (g++ or clang++)
- PHP 7.4+ with GMP extension

### Using Docker
The easiest way to run the parser is via Docker. The container is fully signal-aware and will gracefully save progress if stopped.

```bash
# 1. Build the image
docker build -t btc-parser todo/

# 2. Run the parser
# Mount your bitcoin blocks directory and the output filter directory
docker run \
  -v /absolute/path/to/host/blocks:/app/blocks \
  -v /absolute/path/to/host/filter:/app/filter \
  -v /absolute/path/to/host/chunks:/app/chunks \
  btc-parser
```

### Aborting & Signals
The Docker container and C++ process are fully signal-aware. If you need to stop a long-running parse:
- Press **Ctrl+C** (SIGINT) or send a **SIGTERM**.
- The parser will finish the current file and gracefully save progress/logs before exiting.
- We use `tini` in the Dockerfile to ensure signals are correctly propagated to the C++ PID.

## Manual Commands (without Docker)
If running locally, compile first:
`g++ -std=c++17 -O3 main.cpp -o main`

1. **`./main download`**  
   Downloads sample `blk*.dat` files to get you started.

2. **`./main parse [--debug]`**
   Scans the `blocks/` directory, auto-detects all chunks, skips already-completed ones, and processes the rest. Safe to run repeatedly — re-running monthly picks up new blocks automatically. The `--debug` flag prints the first address found per type per file.

   To manually run a specific chunk: **`./main parse <chunk_index> [--debug]`**

3. **`./main test <address_or_hash160>`**  
   Instantly verifies if an address exists in the generated filters.

## Incremental Updates
Re-run `./main parse` (or the Docker container) periodically to pick up new blocks:
- **Completed chunks** (all 1000 files present and parsed): skipped entirely — old blocks never change.
- **Last partial chunk**: always re-parsed — Bitcoin Core keeps appending new blocks to the last `.dat` file until it fills up, so a file-count check alone is not sufficient.
- **New chunks**: processed automatically.

This makes monthly re-runs safe and efficient with no manual bookkeeping.

## Troubleshooting
- **Missing Blocks**: Ensure your `blocks/` directory contains `blk00000.dat`, `blk00001.dat`, etc.
- **PHP GMP**: Ensure `extension=gmp` is enabled in your `php.ini`.
- **Permissions**: Ensure the `filter/` and `chunks/` directories are writable by the process.