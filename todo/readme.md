# Bitcoin Slot Machine - High-Performance Filter Pipeline

This C++ and PHP toolset processes terabytes of raw Bitcoin data (`blk*.dat`), extracts 20-byte Hash160 address payloads, and intelligently builds a **multi-tiered 64-bit Bloom Filter Hierarchy (32 MB to 2048 MB)** for instantaneous, 0-RAM lookups on any conventional Web Server.

## The Unified Command-Line Tool

Everything has been consolidated into a single zero-dependency C++ application: `main.cpp`.
Compile it once natively with C++17 support and maximum (-O3) hardware optimizations:
```bash
g++ -std=c++17 -O3 main.cpp -o main
```

### 1. `download` (Optional)
Downloads missing example blockchain files (`blk*.dat`) into the `blocks/` directory.
```bash
./main download
```

### 2. `parse [--debug]`
The core processing engine. Parses the massive `.dat` files sequentially in chunks (1000 files each).
Instead of saving uncompressed raw payloads to disk, it natively applies a **64-bit FNV-1a Hash** and **simultaneously builds 7 different Bloom Filter Arrays (ranging from 32 MB up to 2048 MB)** in RAM. It then saves them directly to the `filter/` folder.
* **Crash-proof:** If manually cancelled midway, it seamlessly reparses only incomplete chunks on the next run based on textual `.log` receipts stored inside the `chunks/` folder.
```bash
./main parse          # Scans and parses all available untouched blocks automatically
./main parse --debug  # Prints debug tracking of the first addresses found per file in realtime
```

### 3. `test <address_or_hash_hex>`
An ultra-fast O(1) command-line diagnostic tool simulating exactly what the PHP server does. Instead of loading gigabytes of data into RAM, it instantly streams the exact 8 bytes isolated directly off the SSD. It evaluates your test-address uniformly against all 7 file variants to vividly illustrate the mathematical "False-Positive" filter rate threshold.
```bash
# Test a Base58 Address
./main test 1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa

# Test a 20-byte Hash160 (Hex string)
./main test 62e907b15cbf27d5425399ebf6f0fb50ebb88f18
```

## Running on a Bare-Metal Node via Docker

If you want to effortlessly deploy the C++ parser securely onto a pristine node without managing C++ libraries manually, use the provided lightweight Alpine `Dockerfile`.

1. **Build the container image (compiles the code optimized):**
```bash
docker build -t btc-parser .
```
2. **Execute the parser using folder volume-mounts:**
```bash
docker run \
  -v /absolute/path/to/host/blocks:/app/blocks \
  -v /absolute/path/to/host/filter:/app/filter \
  -v /absolute/path/to/host/chunks:/app/chunks \
  btc-parser
```
Because the underlying Docker `CMD` is preconfigured to `"parse"`, the container autonomously boots up, processes all unaccounted blockchain `.dat` files, updates your 7 respective filter `.bin` files, writes logging receipts to your `/chunks` folder, and gracefully terminates upon completion.

## Web Server API (index.php)
To deploy the database for slot-machine processing, simply upload `index.php` alongside the `filter/` directory (e.g. shipping the `2048mb.bin` file, or fallback smaller files like `64mb.bin` or `32mb.bin`) to your PHP host (e.g. ALL-INKL).

The `index.php` executes an absolute O(1) mathematical `fseek` disk-lookup onto the massive binary files utilizing GMP (GNU Multiple Precision). **It verifies thousands of addresses instantly using exactly 0 MB of allocated RAM**.

**Usage:** `https://your-domain.com/index.php?address_hex=59b9b40f93d4a8989e02773a153b54a273ad1736`