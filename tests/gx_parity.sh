#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# GX-1 parity cases: the firmware (host simulator) generates, plays 8 bars, and its MIDI is checked against
# the engine's render (tests/gx_parity.py). Needs build/host/seq4_host (host/build_host.sh).
#   tests/gx_parity.sh
set -u
cd "$(dirname "$0")/.."
FAIL=0
# genre seed part(0..5) map
for c in "deep_dnb 1 1 tonverk" "deep_dnb 7 4 tonverk" "breaks 1 1 tonverk" "breaks 3 4 gm" "breaks 5 3 tonverk" \
         "autonomic 2 1 tonverk" "autonomic 4 4 gm" "deep_dnb 9 2 gm"; do
    set -- $c
    d="build/gx/$1_s$2_p$3_$4"
    mkdir -p "$d"
    case "$1" in breaks) ms=15200 ;; autonomic) ms=11600 ;; *) ms=11500 ;; esac
    cat > "$d/case.s4" <<EOF
wait 300
gx map $4
gx new $1 $2 $3
gx dump expected.txt
tap PLAY
wait $ms
tap PLAY
wait 200
EOF
    if ! build/host/seq4_host "$d/case.s4" "$d" > "$d/host.log" 2>&1; then
        echo "  FAIL host run $d"; tail -5 "$d/host.log"; FAIL=1; continue
    fi
    python3 tests/gx_parity.py "$d" || FAIL=1
done
exit $FAIL
