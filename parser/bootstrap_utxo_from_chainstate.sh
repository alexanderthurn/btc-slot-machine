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

if [[ ! -s "$UTXO_DUMP_CSV" ]]; then
  echo "UTXO dump CSV was not created or is empty: $UTXO_DUMP_CSV"
  exit 1
fi

echo "[4/5] Building parser binary (with sqlite export support)"
g++ -std=c++17 -O3 "$PARSER_DIR/main.cpp" -o "$PARSER_DIR/main" -lsqlite3

echo "[5/5] Importing CSV into parser balance artifacts"
(
  cd "$PARSER_DIR"
  ./main import_utxo_dump "$UTXO_DUMP_CSV"
)

echo
echo "Done."
echo "Deployable outputs in web/filter:"
echo "  - *mb.bin, *mb_bal.bin, balance_utxo.sqlite"
echo "Parser-only UTXO state/checkpoints in parser/state."
