#!/usr/bin/env bash
# Launches SauceEngine with the bundled test map (maps/sauce_test.bsp).
#
# Usage:
#   ./run_test_map.sh [/path/to/game] [extra engine args...]
#
# The game directory defaults to ./hl2-local. Any folder with a valid
# gameinfo.txt works; the test map itself is loaded from ./maps.
set -euo pipefail

cd "$(dirname "$0")"

GAME_DIR="${1:-${SAUCE_GAME_DIR:-$PWD/hl2-local}}"
if [ $# -gt 0 ]; then shift; fi

if [ ! -f "$GAME_DIR/gameinfo.txt" ]; then
    echo "Game directory '$GAME_DIR' does not contain gameinfo.txt" >&2
    echo "Usage: $0 [/path/to/game] [extra engine args...]" >&2
    exit 1
fi

exec ./SauceEngine -game "$GAME_DIR" -map sauce_test "$@"
