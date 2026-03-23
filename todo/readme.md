# Bitcoin Slot Machine - Preprocessing Pipeline

This toolset processes raw Bitcoin data (`blk*.dat`), extracts address payloads, and compresses them into a highly space-efficient Bloom filter / Hash table format (`final_filter_table.bin`) for the Slot Machine web game.

## The Unified Command-Line Tool

Everything has been consolidated into a single zero-dependency C++ application: `main.cpp`.
Compile it once with C++17 support:
```bash
g++ -std=c++17 main.cpp -o main
```

### Usage Instructions

**1. `download`** (Optional)
Downloads missing example blockchain files (`blk*.dat`) into the `blocks/` directory.
```bash
./main download
```

**2. `parse <chunk_index> [--debug]`**
Scans the raw `.dat` data for Transaction Outputs (TxOut) and saves exactly the 20-byte "Hash160-payloads" of valid Bitcoin addresses. Because the blockchain is huge, execution is split into "chunks" of 1000 blocks each to prevent data loss on crash.
```bash
./main parse 0          # Parses blocks 0 to 999
./main parse 1 --debug  # Parses blocks 1000 to 1999 and prints found addresses realtime
```

**3. `build`**
Collects all extracted payloads from the `chunks/` directory and processes them through the fast, low-memory **FNV-hash function**. It ultimately creates the exact 512 MB array (`final_filter_table.bin`) the web game needs.
```bash
./main build
```

**4. `test <address_or_hash_hex>`**
Loads the processed `final_filter_table.bin` directly into the computer's RAM and checks whether an address or Hash160 was ever funded on the Bitcoin network in a fraction of a second.
```bash
# Test a Base58 Address
./main test 1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa

# Test a 20-byte Hash160 (Hex string)
./main test 62e907b15cbf27d5425399ebf6f0fb50ebb88f18
```

## Integration (Web Game)
The web game no longer needs to process thousands of files. It reserves memory, directly loads the `final_filter_table.bin` as a highly compressed lookup table, and executes `testKey()` to rapidly evaluate if any randomly generated public key/address was ever active on the blockchain.