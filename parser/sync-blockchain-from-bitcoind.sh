#!/bin/bash

BLOCKCHAIN_SOURCE="${BLOCKCHAIN_SOURCE:-/bitcoin/core/blocks/}"
BLOCKCHAIN_DEST="${BLOCKCHAIN_DEST:-./blocks/}"

rsync -ah --info=progress2 --inplace --whole-file --no-compress "$BLOCKCHAIN_SOURCE" "$BLOCKCHAIN_DEST"
