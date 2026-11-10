#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the SEQ-4 host simulator (the whole app on this machine) into build/host/seq4_host.
# The same sources as the firmware: the app in one unit, the sequencer (seq/seq4.c) in another, as on the
# device; -ffp-contract=off like the device (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DEN_HOST -DGX_HIST=16 -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/gx -Ibuild/gen -o build/host/seq4_host \
    host/seq4_host.c firmware/src/seq/seq4.c firmware/src/gx/gx_rng.c firmware/src/gx/gx_engine.c firmware/src/gx/gx_drums.c firmware/src/gx/gx_pitched.c firmware/src/gx/gx_mutate.c firmware/src/gx/gx_profiles_gen.c -lm
echo "host: build/host/seq4_host"
