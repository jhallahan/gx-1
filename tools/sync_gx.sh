#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copy the GX genre engine (jhallahan/fm1-genre-ator, GPL-3.0-or-later) into firmware/src/gx/.
#   tools/sync_gx.sh [PATH-TO-fm1-genre-ator]     (default: ../fm1-genre-ator)
# The engine is developed and tested there (host CLI, WASM player, its own tests); this tree carries a copy
# of the files the firmware needs, and firmware/src/gx/VERSION says which engine commit they came from.
set -e
cd "$(dirname "$0")/.."
SRC="${1:-../fm1-genre-ator}"
[ -f "$SRC/engine/include/gx.h" ] || { echo "sync_gx: no engine at $SRC" >&2; exit 1; }
mkdir -p firmware/src/gx
for f in include/gx.h include/gx_profile.h src/gx_rng.c src/gx_engine.c src/gx_drums.c src/gx_pitched.c \
         src/gx_mutate.c src/gx_profiles_gen.c; do
    cp "$SRC/engine/$f" firmware/src/gx/
done
{
    echo "engine: jhallahan/fm1-genre-ator $(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "files:  copied by tools/sync_gx.sh; edit them in the engine repository, not here"
} > firmware/src/gx/VERSION
echo "sync_gx: $(cat firmware/src/gx/VERSION | head -1)"
