# Plan: Exact balance amounts (export + fast lookup)

This document describes how to add **per-UTXO satoshi values** and optional **aggregated “balance per lookup key”** on top of the existing balance pipeline, without replacing the Bloom filters. Implementation is **deferred** until you confirm whether the web server has SQLite (or you choose flat-file binary search only).

---

## Goals

1. **Offline (parser):** While building the UTXO set, retain each output’s **value in satoshis** next to the data you already store for balance blooms (`key32`).
2. **Export:** Emit an artifact suitable for the web host:
   - **Option A:** SQLite database (if `pdo_sqlite` / `sqlite3` is available).
   - **Option B:** Sorted fixed-width binary file + PHP `fseek`/`fread` binary search (no extensions).
3. **Online (PHP):** Answer “does this key have funds in our snapshot?” with **certainty** (no Bloom false positives for positives you confirm via exact store), and optionally return **amount** (per-UTXO or aggregated).

---

## Current state (baseline)

- `buildBalanceFilters` keeps `unordered_map<UTXOKey, array<uint8_t,32>>` — **outpoint → bloom key material only**.
- Output **value** is read from the wire when parsing each output but **not** retained in the map.
- Bloom files `*_mb_bal.bin` remain **probabilistic**; this plan **adds** an exact layer, not a replacement.

---

## Step 1 — Decide the “balance identity” key

Pick one export key model (document it in code comments):

| Model | Pros | Cons |
|--------|------|------|
| **`key32` only** (same 32-byte buffer you already use for blooms) | One column, matches parser logic today | Taproot / P2PKH etc. all live in one 32-byte space; fine if you treat “identity” as that blob |
| **Separate columns** (`script_type`, `hash160` or `xonly32`, …) | Clearer semantics | More schema work and joins |

**Recommendation:** start with **`key32` BLOB PRIMARY KEY** (or 32-byte row in flat file) for v1; add richer typing only if you need it.

**Aggregation:** “Balance per `key32`” = `SUM(value_sat)` over all UTXO rows sharing that `key32` (one pass over the final map).

---

## Step 2 — Parser: store `value_sat` per UTXO

1. Replace map value type from `array<uint8_t,32>` to a small struct, e.g.  
   `{ array<uint8_t,32> key32; uint64_t value_sat; }`.
2. On **insert** (when you currently `utxoMap[key] = out.key32`), also set `value_sat` from the `val` you already read for that output.
3. On **spend** (`erase`), no change to erase semantics — the row already carries the value until removed.

**Checkpoint files:** today checkpoints serialize 36-byte outpoint + 32-byte `key32`. Extend format (bump magic/version) to include **8-byte LE `value_sat`** per entry, and update load/save paths (`balance_utxo_*`, progress) accordingly.

**Risk:** checkpoint file size grows by **~8/68 ≈ 12%** vs current 68-byte rows → still acceptable at your scale.

---

## Step 3 — Export pass (after UTXO map is final)

Run once at end of successful `buildBalanceFilters` (after chain scan, before or after writing Bloom files — order is flexible):

1. **Optional aggregate:** Walk `utxoMap` and fill `unordered_map<array<uint8_t,32>, uint64_t>` keyed by `key32`, summing `value_sat` (RAM ~similar to one extra numeric map over distinct keys — usually smaller than full UTXO count).
2. **Emit artifact:**
   - **SQLite:** `CREATE TABLE utxo(key32 BLOB PRIMARY KEY, sat INTEGER NOT NULL);` — either one row per **aggregated** key, or per-outpoint table + `CREATE INDEX` / materialized query. For static snapshot, **pre-aggregated single table** is simplest for PHP.
   - **Flat file:** sort rows by `key32`, fixed width 32+8 = 40 bytes; document endianness (LE sat).

**File placement:** e.g. `web/filter/balance_exact.sqlite` or `web/filter/balance_sorted_keys.bin` — same deploy path as existing filters.

---

## Step 4 — PHP lookup (no new extensions)

### If SQLite is enabled

- Open DB read-only (`PDO('sqlite:...')`).
- `SELECT sat FROM balance WHERE key32 = ?` (bind blob).
- Return JSON to the client; treat **no row** as **0 / not in set** for that snapshot.

### If SQLite is not available

- Implement **`binarySearchKey32($fh, $needle32)`** on the sorted 40-byte file.
- Return sat value from the matching row.

---

## Step 5 — Wire into `test.html` / API (optional)

- After Bloom says “maybe”, or always for “exact mode” toggle: call **`exact_lookup.php`** (new small endpoint) that uses SQLite or flat binary.
- Clearly label results as **“snapshot at block height / export time X”** so users do not confuse with live chain.

---

## Step 6 — Testing & validation

1. **Unit parity:** Pick 100 random `key32` from map export; verify `SUM` matches re-scan of a tiny regtest/mock subset if you have one; otherwise spot-check against `bitcoin-cli gettxoutsetinfo` / known addresses (manual).
2. **Size check:** Log distinct `key32` count and exported file size after first mainnet export.
3. **PHP:** Load test on host: `extension_loaded('pdo_sqlite')` and a trivial `SELECT 1`.

---

## Effort summary

| Piece | Rough size |
|--------|----------------|
| Map value + insert/spend + checkpoint format | Small |
| Export aggregate + write SQLite or sorted bin | Medium |
| PHP exact lookup endpoint | Small |
| Docs / snapshot disclaimer in UI | Small |

**Not** a second full-chain replay; cost is mostly **RAM/CPU for one O(N) walk** and **disk write** of the export.

---

## Deferred until you verify SQLite

- [ ] On server: confirm `pdo_sqlite` or `sqlite3` in `phpinfo()`.
- [ ] If **no**: implement flat-file binary search path first; SQLite can be added later as an alternative exporter.

---

## Open questions (answer when implementing)

1. Export **per outpoint** (auditable) vs **aggregated per `key32`** (smaller, simpler queries)?
2. Do you need **block height / export timestamp** in the DB header table for the web UI?
3. Should exact lookup be **public** or behind auth/rate limit (large DB file attracts scraping)?
