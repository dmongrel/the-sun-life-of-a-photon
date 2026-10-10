#!/usr/bin/env bash
# Captures one frame at a loop time: tools/capture.sh <loop seconds> <out.png> [VAR=value ...]
# Runs the saver in its /w window with SUN_PHOTON_CAPTURE set; it exits once the frame is written.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
at="$1"; out="$2"; shift 2
env SUN_PHOTON_CAPTURE="$(cygpath -w "$out")" SUN_PHOTON_CAPTURE_AT="$at" SUN_PHOTON_LOG=1 "$@" \
    "$here/${SUN_BUILD:-build}/the-sun.scr" /w
