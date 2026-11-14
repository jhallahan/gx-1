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
         "autonomic 2 1 tonverk" "autonomic 4 4 gm" "deep_dnb 9 2 gm" "dub_techno 1 1 tonverk" "dub_techno 6 3 gm" \
         "idm 1 1 tonverk" "electro 2 1 gm" "uk_garage 3 1 tonverk" "minimal 4 2 tonverk" "downtempo 5 1 gm" \
         "jungle 1 1 tonverk" "halftime 2 1 gm" "deep_house 3 1 tonverk" "acid 4 1 tonverk" "dubstep 5 4 gm" \
         "liquid 1 1 tonverk" "rollers 2 1 gm" "detroit 3 1 tonverk" "hard_techno 4 1 tonverk" "lofi_house 5 1 gm" "ambient 6 1 tonverk" \
         "psybient 1 1 tonverk" "glitch_ambient 2 1 gm" \
         "pop 1 1 gm" "rnb 2 1 tonverk" "synthpop 3 1 gm" \
         "jungle 3 1 tonverk fill" "deep_house 2 1 gm fill" "breaks 4 1 tonverk fill"; do
    set -- $c
    fill="${5:-}"
    d="build/gx/$1_s$2_p$3_$4${fill:+_fill}"
    mkdir -p "$d"
    case "$1" in breaks) ms=15200 ;; autonomic) ms=11600 ;; dub_techno) ms=16200 ;; idm) ms=15450 ;; electro) ms=15200 ;; uk_garage) ms=15000 ;;
                   minimal) ms=15950 ;; downtempo) ms=21300 ;;
                   jungle) ms=12050 ;; halftime) ms=11700 ;; deep_house) ms=16200 ;; acid) ms=15450 ;; dubstep) ms=14150 ;;
                   liquid|rollers) ms=11450 ;; detroit) ms=15200 ;; hard_techno) ms=14100 ;; lofi_house) ms=16400 ;;
                   ambient) ms=24400 ;; psybient) ms=20400 ;; glitch_ambient) ms=19600 ;; pop) ms=16670 ;; rnb) ms=22220 ;; synthpop) ms=17540 ;; *) ms=11500 ;; esac
    cat > "$d/case.s4" <<EOF
wait 300
gx map $4
gx new $1 $2 $3
gx dump expected.txt
tap PLAY
EOF
    if [ -n "$fill" ]; then   # PLAY held through the phrase: the fill (in after a hold, before the first fill region)
        printf 'press PLAY\nwait %s\nrelease PLAY\nwait 100\ntap PLAY\nwait 200\n' "$ms" >> "$d/case.s4"
    else
        printf 'wait %s\ntap PLAY\nwait 200\n' "$ms" >> "$d/case.s4"
    fi
    if ! build/host/seq4_host "$d/case.s4" "$d" > "$d/host.log" 2>&1; then
        echo "  FAIL host run $d"; tail -5 "$d/host.log"; FAIL=1; continue
    fi
    python3 tests/gx_parity.py "$d" $fill || FAIL=1
done
exit $FAIL
