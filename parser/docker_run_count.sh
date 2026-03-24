#!/bin/bash

git -C ../ pull

docker build -t btc-parser ./

docker run \
  -v "$(pwd)/blocks:/app/blocks" \
  -v "$(pwd)/counts:/app/counts" \
  btc-parser count
