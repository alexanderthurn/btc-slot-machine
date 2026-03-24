#!/bin/bash

FILTER_SOURCE="${FILTER_SOURCE:-../web/filter/}"
FILTER_DEST="${FILTER_DEST:-user@host:/www/htdocs/filter}"

rsync -avzP "$FILTER_SOURCE" "$FILTER_DEST"
