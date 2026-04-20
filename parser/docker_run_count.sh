#!/bin/bash

git -C ../ pull

docker build -t btc-parser ./

CONTAINER_NAME="${CONTAINER_NAME:-btc-parser-count}"
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

docker run --rm \
  --name "$CONTAINER_NAME" \
  -v "$(pwd)/blocks:/app/blocks" \
  -v "$(pwd)/counts:/app/counts" \
  btc-parser count
