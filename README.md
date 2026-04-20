# Bitcoin Slot Machine - High-Performance Filter Pipeline

This C++ and PHP toolset processes terabytes of raw Bitcoin data (`blk*.dat`), extracts 20-byte Hash160 address payloads, and builds high-capacity 64-bit Bloom filters (main + balance tiers) for instantaneous, 0-RAM web lookups.

## The Unified Command-Line Tool

### Core Features
1. **Slot Machine (`index.html`)**:
   - Generates a random BIP39 mnemonic (12 words) on each spin.
   - Derives BIP32 HD addresses (P2PKH, P2SH-P2WPKH, P2WPKH) for paths m/44', m/49', m/84'.
   - Checks 60 addresses per spin against the locally loaded bloom filter.
   - Local filter cached in IndexedDB — no re-download on reload.

2. **Filter Test (`test.html`)**:
   - Direct address lookup: Base58, Bech32, Hash160, or public key input.
   - Client-side lookup (JS, sub-millisecond) vs. server-side (`test.php`, all filter sizes).
   - Shows per-filter results from server for comparison and debugging.
   - Persistent history with mempool links.

3. **C++ Preprocessing (`main.cpp`)**:
   - Parallelized parsing using all available CPU cores via `std::thread`.
   - Thread-safe Bloom filter updates using `std::atomic<uint64_t>`.
   - Main filter arrays are kept in RAM during parse and saved as `filter/*mb.bin` checkpoints.
   - Graceful termination (SIGINT/SIGTERM): finishes the current file and saves progress before exiting.
   - Incremental updates: re-running automatically skips fully completed chunks and only re-parses the last partial chunk (which may have grown since the last run) plus any new chunks.
   - Extracts 20-byte Hash160 payloads directly from Bitcoin `.dat` files (P2PKH, P2SH, P2WPKH).
   - Fully dynamic multi-tiered filter generation (32MB up to 2GB).
   - O(1) direct-to-disk verification for testing addresses without loading massive filters into RAM.

4. **Backend API (`index.php`)**:
   - Used by the slot machine. Queries the largest available filter via O(1) disk seek.
   - Zero-RAM footprint. Typically < 1ms processing time.

5. **Debug API (`test.php`)**:
   - Used by the filter test page. Queries all available filter sizes in one request.
   - Returns per-filter results so you can compare accuracy across sizes.

6. **Balance filters & UTXO SQLite**:
   - Balance artifacts are built from chainstate dump import/bootstrap (not from slow full blk replay).
   - State/checkpoints live under `parser/state/` (`balance_utxo_tip.chk`, etc.); format version **3** stores `key32` plus **`value_sat`** per coin.
   - When the parser is built **with** SQLite (`sqlite3` headers + `-lsqlite3`), it writes **`filter/balance_utxo.sqlite`** for deployment: one row per unspent tracked output (`txid`, `vout`, `key32`, `value_sat`). Internal temp/state files stay in `parser/state/`.
   - **`web/balance_lookup.php`** (PDO SQLite) returns JSON for a given **`?txid=`** (64 hex), optionally **`&vout=`** and **`&key32=`**. Copy or symlink `filter/balance_utxo.sqlite` next to the script under `web/filter/` if you deploy PHP separately from the parser output path.
   - Use chainstate bootstrap (`./main import_utxo_dump <csv>` or `bootstrap_utxo_from_chainstate.sh`) to seed/refresh balance UTXO state from a chainstate dump CSV.
   - The old full blk replay UTXO pass was removed; balance artifacts are now maintained via chainstate dump import/bootstrap only.

## Installation & Setup

### Prerequisites
- Docker (recommended) OR
- C++17 Compiler (g++ or clang++) and, for **`balance_utxo.sqlite`**, SQLite development libraries (e.g. macOS: Xcode CLTs / Homebrew `sqlite`; Linux: `libsqlite3-dev`) so `<sqlite3.h>` is available at compile time
- PHP 7.4+ with GMP extension (`index.php` / slot machine); optional PDO SQLite for **`balance_lookup.php`**

### Using Docker
The easiest way to run the parser is via Docker. The container is fully signal-aware and will gracefully save progress if stopped.

```bash
cd parser && bash docker_run.sh
```

This script pulls the latest code, rebuilds the image, and runs the parser with the correct volume mounts (`parser/blocks`, `parser/chunks`, `parser/state`, `web/filter`).

### Chainstate UTXO Bootstrap (recommended)
Use this helper to build/refresh balance UTXO artifacts from a cloned chainstate:

```bash
export CHAINSTATE_SOURCE="/mnt/laser/bitcoin/core/chainstate/"
(cd btc-slot-machine/parser && bash bootstrap_utxo_from_chainstate.sh)
```

What it does:
- clones `CHAINSTATE_SOURCE` into `parser/state/chainstate-clone/`
- builds a Docker image for [`bitcoin-utxo-dump`](https://github.com/in3rsha/bitcoin-utxo-dump)
- dumps CSV (`txid,vout,amount,type,script`) to `parser/state/utxodump.csv`
- runs `./main import_utxo_dump ...` to generate `web/filter/*mb_bal.bin`, `web/filter/balance_utxo.sqlite`, and `parser/state/balance_utxo_tip.chk`

Note: this workflow still keeps parser state in `parser/state/` and deployable files in `web/filter/`.

### Aborting & Signals
The Docker container and C++ process are fully signal-aware. If you need to stop a long-running parse:
- Press **Ctrl+C** (SIGINT) or send a **SIGTERM**.
- The parser will finish the current file and gracefully save progress/logs before exiting.
- We use `tini` in the Dockerfile to ensure signals are correctly propagated to the C++ PID.

## Manual Commands (without Docker)
If running locally, compile from the `parser/` directory first:

- **With** SQLite export (recommended):  
  `cd parser && g++ -std=c++17 -O3 main.cpp -o main -lsqlite3`
- **Without** SQLite headers: the same command without `-lsqlite3` still builds; balance blooms and checkpoints work, but **`balance_utxo.sqlite` is not produced**.

1. **`./main download`**  
   Downloads sample `blk*.dat` files to get you started.

2. **`./main parse [--debug]`**
   Scans the `blocks/` directory, auto-detects all chunks, skips already-completed ones, and processes the rest. Safe to run repeatedly — re-running monthly picks up new blocks automatically. The `--debug` flag prints the first address found per type per file.
   This command updates the main address filters (`*mb.bin`). Balance UTXO artifacts are refreshed via the chainstate bootstrap workflow.

   To manually run a specific chunk: **`./main parse <chunk_index> [--debug]`**

3. **`./main test <address_or_hash160>`**
   Instantly verifies if an address exists in the generated filters.

4. **`./main import_utxo_dump <csv_path>`**
   Imports a chainstate UTXO dump CSV (for example from `bitcoin-utxo-dump`) into the parser's balance UTXO state and outputs.

5. **`./main count`**
   Scans all `blk*.dat` files in `blocks/` and counts every output type, unique addresses, transactions, inputs with visible public keys, and total BTC ever moved. Uses all available CPU cores. Saves a per-file checkpoint to `counts/` (up to ~25 GB disk) so a re-run skips already-processed files. Prints a full summary at the end:

   ```
   =================================================================
     Bitcoin Blockchain Count
   =================================================================
     Blocks:                       850,000
     Transactions:                 1,000,000,000
       Coinbase:                   850,000
       SegWit:                     600,000,000

     Inputs:                       2,100,000,000
       w/ visible pubkey:          500,000,000

     Outputs:                      2,500,000,000
       zero-value:                 12,000,000
       P2PKH:                      1,200,000,000
       P2SH:                         400,000,000
       P2WPKH:                       600,000,000
       P2WSH:                         50,000,000
       P2PK compressed:               10,000,000
       P2PK uncompressed:             20,000,000
       P2TR (Taproot):                80,000,000
       OP_RETURN:                     30,000,000
       Other:                          5,000,000

     Unique addr (P2PKH+P2SH+P2WPKH): 400,000,000

     Total output value:           2,100,000.00000000 BTC
   =================================================================
   ```

   > **RAM note:** the unique address deduplication loads all hash160s into memory for sorting (~30 GB peak). Make sure you have sufficient free RAM before running.

## Syncing Blocks from an External Source

If your Bitcoin block files live on a separate machine or mount, you can sync them using a wrapper script that sets the source/destination before calling the helper in `parser/misc/`:

```bash
#!/bin/bash

export BLOCKCHAIN_SOURCE="/mnt/your-bitcoin-node/blocks/"
export BLOCKCHAIN_DEST="./blocks/"

(cd btc-slot-machine/parser && bash sync-blockchain-from-bitcoind.sh)
```

The subshell `( )` keeps your working directory unchanged after the call. Override `BLOCKCHAIN_SOURCE` and `BLOCKCHAIN_DEST` to match your own paths.

## Balance UTXO refresh
`./main parse` updates main address filters.  
Balance artifacts (`*mb_bal.bin`, `balance_utxo.sqlite`) are refreshed via:
- `bash bootstrap_utxo_from_chainstate.sh`, or
- `./main import_utxo_dump <csv_path>`

## Incremental Updates
Re-run `./main parse` (or the Docker container) periodically to pick up new blocks:
- **Completed chunks** (all 1000 files present and parsed): skipped entirely — old blocks never change.
- **Last partial chunk**: always re-parsed — Bitcoin Core keeps appending new blocks to the last `.dat` file until it fills up, so a file-count check alone is not sufficient.
- **New chunks**: processed automatically.

This makes monthly re-runs safe and efficient with no manual bookkeeping.

## Troubleshooting
- **Missing Blocks**: Ensure `parser/blocks/` contains `blk00000.dat`, `blk00001.dat`, etc.
- **PHP GMP**: Ensure `extension=gmp` is enabled in your `php.ini`.
- **Permissions**: Ensure `web/filter/` and `parser/chunks/` are writable by the process.


## Test Data

bc1qe998udjkuw6x208q33ury4pwkpl6naq7wqe23d