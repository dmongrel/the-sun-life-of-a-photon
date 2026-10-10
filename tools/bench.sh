#!/usr/bin/env bash
# Runs the saver in its /w window for a while from a loop time and prints the GPU pass timings
# and frame rate from the log: tools/bench.sh [loop seconds, default 290] [run seconds, default 15]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
at="${1:-290}"; run="${2:-15}"
log="$(cygpath -u "$TEMP")/the-sun.log"
rm -f "$log"
# The capture is only the exit signal: it fires `run` seconds in.
out="$(mktemp -u).png"
env SUN_PHOTON_TIME="$at" SUN_PHOTON_CAPTURE="$(cygpath -w "$out")" SUN_PHOTON_CAPTURE_AT="$((at + run))" \
    SUN_PHOTON_LOG=1 "$here/build/the-sun.scr" /w
rm -f "$out"
grep -E "gpu ms|fps" "$log"
