#!/usr/bin/env bash
# Build and run amble's host-side unit tests (pure logic only; no ESPHome, no IDF).
set -euo pipefail
cd "$(dirname "$0")/../.."
SRCS=(components/amble/telink.cpp)
for extra in components/amble/fixture.cpp components/amble/guard.cpp components/amble/store.cpp components/amble/mesh_node.cpp components/amble/light.cpp components/amble/pin.cpp components/amble/identity.cpp components/amble/cloud.cpp; do
  [ -f "$extra" ] && SRCS+=("$extra")
done
OUT="${TMPDIR:-/tmp}/amble-host-tests"
c++ -std=c++20 -Wall -Wextra -DAMBLE_HOST_TEST -DMINI_TEST_MAIN -I components/amble -I tests/host -I tests/host/third_party \
  tests/host/main.cpp tests/host/test_*.cpp "${SRCS[@]}" -o "$OUT"
"$OUT"
