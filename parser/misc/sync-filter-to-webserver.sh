#!/bin/bash

FILTER_SOURCE="${FILTER_SOURCE:-./filter/}"
FILTER_DEST="${FILTER_DEST:-user@host:/www/htdocs/filter}"

rsync -avzP --append-verify "$FILTER_SOURCE" "$FILTER_DEST"
