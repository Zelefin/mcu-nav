#!/usr/bin/env sh
set -eu

PMTILES_BIN="${PMTILES_BIN:-pmtiles}"
SOURCE_URL="${SOURCE_URL:-https://build.protomaps.com/20260702.pmtiles}"
OUT="${OUT:-control-app/kyiv-oblast.pmtiles}"
BBOX="${BBOX:-29.1,49.1,32.3,51.6}"
MAXZOOM="${MAXZOOM:-16}"

"$PMTILES_BIN" extract "$SOURCE_URL" "$OUT" --bbox="$BBOX" --maxzoom="$MAXZOOM"
sha256sum "$OUT"
