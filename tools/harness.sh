#!/usr/bin/env bash
# Interactive review harness.
#
#   ./tools/harness.sh          fixture tracks, offline, deterministic
#   ./tools/harness.sh --live   your real Spotify
#
# Fixtures are the default because reviewing states should not depend on what
# happens to be playing, and scrubbing the clock against a live player fights
# the next poll.

set -euo pipefail
cd "$(dirname "$0")/.."
export HOMEBREW_PREFIX="${HOMEBREW_PREFIX:-/opt/homebrew}"

LIVE=0
[ "${1:-}" = "--live" ] && LIVE=1

pio run -e native >/dev/null

export EMU_HARNESS=1
[ "$LIVE" -eq 1 ] || export EMU_FAKE=1

exec ./.pio/build/native/program
