#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SEQ-4 in the browser: the host simulator compiled to WebAssembly (Emscripten), plus its page.
#   [EN_VERSION=0.1] web/emu/build.sh  ->  build/emu/{index.html, worklet.js, seq4.wasm}
# Same sources and flags as host/build_host.sh (-ffp-contract=off, like the device).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DEN_HOST -DEN_WEB -DGX_HIST=16 "-DEN_VERSION=\"$(printf %s "${EN_VERSION:-DEV}" | tr a-z A-Z)\"" -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/gx -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/seq4.wasm web/emu/seq4_web.c firmware/src/seq/seq4.c firmware/src/gx/gx_rng.c firmware/src/gx/gx_engine.c firmware/src/gx/gx_drums.c firmware/src/gx/gx_pitched.c firmware/src/gx/gx_mutate.c firmware/src/gx/gx_profiles_gen.c
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/seq4.wasm) B wasm)"
