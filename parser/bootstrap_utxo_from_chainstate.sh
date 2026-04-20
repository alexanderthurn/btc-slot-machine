#!/bin/bash
set -euo pipefail

PARSER_DIR="$(cd "$(dirname "$0")" && pwd)"
STATE_DIR="${STATE_DIR:-$PARSER_DIR/state}"
CHAINSTATE_SOURCE="${CHAINSTATE_SOURCE:-}"
CHAINSTATE_CLONE_DIR="${CHAINSTATE_CLONE_DIR:-$STATE_DIR/chainstate-clone}"
UTXO_DUMP_CSV="${UTXO_DUMP_CSV:-$STATE_DIR/utxodump.csv}"
UTXO_DUMP_FIELDS="${UTXO_DUMP_FIELDS:-txid,vout,amount,type,script}"
IMAGE_NAME="${IMAGE_NAME:-btc-utxo-dump}"
UTXO_CONTAINER_NAME="${UTXO_CONTAINER_NAME:-btc-utxo-dump}"
SKIP_CHAINSTATE_DUMP="${SKIP_CHAINSTATE_DUMP:-0}"

if [[ -z "$CHAINSTATE_SOURCE" ]]; then
  echo "Missing CHAINSTATE_SOURCE."
  echo "Example:"
  echo "  export CHAINSTATE_SOURCE=\"/mnt/laser/bitcoin/core/chainstate/\""
  echo "  (cd btc-slot-machine/parser && bash bootstrap_utxo_from_chainstate.sh)"
  exit 1
fi

if [[ ! -d "$CHAINSTATE_SOURCE" ]]; then
  echo "CHAINSTATE_SOURCE does not exist or is not a directory: $CHAINSTATE_SOURCE"
  exit 1
fi

mkdir -p "$STATE_DIR" "$CHAINSTATE_CLONE_DIR" "$(dirname "$UTXO_DUMP_CSV")"

if [[ "$SKIP_CHAINSTATE_DUMP" != "1" ]]; then
  echo "[1/5] Cloning chainstate to: $CHAINSTATE_CLONE_DIR"
  rsync -a --delete "$CHAINSTATE_SOURCE"/ "$CHAINSTATE_CLONE_DIR"/

  echo "[2/5] Building utxo dump docker image: $IMAGE_NAME"
  docker build -t "$IMAGE_NAME" "$PARSER_DIR/utxo-bootstrap"

  CSV_BASENAME="$(basename "$UTXO_DUMP_CSV")"
  CSV_DIR="$(cd "$(dirname "$UTXO_DUMP_CSV")" && pwd)"

  echo "[3/5] Dumping chainstate UTXO CSV to: $UTXO_DUMP_CSV"
  docker rm -f "$UTXO_CONTAINER_NAME" >/dev/null 2>&1 || true
  docker run --rm \
    --name "$UTXO_CONTAINER_NAME" \
    -v "$CHAINSTATE_CLONE_DIR:/chainstate" \
    -v "$CSV_DIR:/out" \
    "$IMAGE_NAME" \
    -db /chainstate \
    -o "/out/$CSV_BASENAME" \
    -f "$UTXO_DUMP_FIELDS"
else
  echo "[1-3/5] SKIP_CHAINSTATE_DUMP=1, reusing existing CSV: $UTXO_DUMP_CSV"
fi

if [[ ! -s "$UTXO_DUMP_CSV" ]]; then
  echo "UTXO dump CSV was not created or is empty: $UTXO_DUMP_CSV"
  exit 1
fi

echo "[4/5] Preparing parser importer"
if command -v g++ >/dev/null 2>&1; then
  g++ -std=c++17 -O3 "$PARSER_DIR/main.cpp" -o "$PARSER_DIR/main" -lsqlite3
  IMPORT_MODE="host"
else
  echo "  g++ not found on host, using dockerized parser importer."
  docker build -t btc-parser "$PARSER_DIR"
  IMPORT_MODE="docker"
fi

echo "[5/5] Importing CSV into parser balance artifacts"
if [[ "$IMPORT_MODE" == "host" ]]; then
  (
    cd "$PARSER_DIR"
    ./main import_utxo_dump "$UTXO_DUMP_CSV"
  )
else
  ABS_STATE_DIR="$(cd "$STATE_DIR" && pwd)"
  ABS_FILTER_DIR="$(cd "$PARSER_DIR/../web/filter" && pwd)"
  ABS_BLOCKS_DIR="$(cd "$PARSER_DIR/blocks" && pwd)"
  ABS_CHUNKS_DIR="$(cd "$PARSER_DIR/chunks" && pwd)"
  ABS_CSV="$(cd "$(dirname "$UTXO_DUMP_CSV")" && pwd)/$(basename "$UTXO_DUMP_CSV")"
  if [[ "$ABS_CSV" == "$ABS_STATE_DIR/"* ]]; then
    CSV_IN_CONTAINER="/app/state/${ABS_CSV#$ABS_STATE_DIR/}"
    EXTRA_MOUNT_ARGS=()
  else
    CSV_DIR_ABS="$(cd "$(dirname "$UTXO_DUMP_CSV")" && pwd)"
    CSV_BASENAME="$(basename "$UTXO_DUMP_CSV")"
    CSV_IN_CONTAINER="/app/utxo_csv/$CSV_BASENAME"
    EXTRA_MOUNT_ARGS=(-v "$CSV_DIR_ABS:/app/utxo_csv")
  fi
  docker rm -f btc-parser-import >/dev/null 2>&1 || true
  docker run --rm \
    --name btc-parser-import \
    -v "$ABS_BLOCKS_DIR:/app/blocks" \
    -v "$ABS_FILTER_DIR:/app/filter" \
    -v "$ABS_CHUNKS_DIR:/app/chunks" \
    -v "$ABS_STATE_DIR:/app/state" \
    "${EXTRA_MOUNT_ARGS[@]}" \
    btc-parser import_utxo_dump "$CSV_IN_CONTAINER"
fi

echo
echo "Done."
echo "Deployable outputs in web/filter:"
echo "  - *mb.bin, *mb_bal.bin, balance_utxo.sqlite"
echo "Parser-only UTXO state/checkpoints in parser/state."
