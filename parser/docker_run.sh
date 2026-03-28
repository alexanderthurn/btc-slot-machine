#!/bin/bash

git -C ../ pull

docker build -t btc-parser ./

docker run --rm \
  -v "$(pwd)/blocks:/app/blocks" \
  -v "$(pwd)/../web/filter:/app/filter" \
  -v "$(pwd)/chunks:/app/chunks" \
  btc-parser
