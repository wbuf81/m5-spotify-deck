#!/usr/bin/env bash
# Full test suite: logic first (fast, hardware-free), then rendering.
#
#   ./tools/run_tests.sh
#
# The visual layer exists because every display bug found in this project so
# far was invisible to logic tests: toast text ghosting under the timecodes,
# the play glyph vanishing after a like, the clock stuttering in 2s steps.

set -euo pipefail
cd "$(dirname "$0")/.."
export HOMEBREW_PREFIX="${HOMEBREW_PREFIX:-/opt/homebrew}"

echo "== unit tests =="
pio test -e test

echo
echo "== building emulator =="
pio run -e native >/dev/null

echo
echo "== visual regression =="
python3 tools/visual_tests.py
