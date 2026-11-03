#!/usr/bin/env bash
# Fetches the single-header ArduinoJson the host tests compile against, pinned to
# the version ESPHome's esp-idf build uses (managed component bblanchon__arduinojson).
set -euo pipefail
cd "$(dirname "$0")"
VER=7.4.3
OUT=third_party/ArduinoJson.h
mkdir -p third_party
[ -f "$OUT" ] && grep -q "ARDUINOJSON_VERSION \"$VER\"" "$OUT" && { echo "ArduinoJson $VER present"; exit 0; }
curl -sSL -o "$OUT" "https://github.com/bblanchon/ArduinoJson/releases/download/v$VER/ArduinoJson-v$VER.h"
grep -q "ARDUINOJSON_VERSION \"$VER\"" "$OUT" || { echo "download did not yield ArduinoJson $VER"; exit 1; }
echo "fetched ArduinoJson $VER -> $OUT"
