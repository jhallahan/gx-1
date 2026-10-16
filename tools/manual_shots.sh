#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The manual's and the site's screenshots, from the simulator (tests/manual.s4), into docs/img; then the panel
# figure (tools/gen_manual_figures.py) with the first sequencer's screen in it.
#   tools/manual_shots.sh
set -e
cd "$(dirname "$0")/.."
sh host/build_host.sh >/dev/null
mkdir -p build/manual docs/img
build/host/seq4_host tests/manual.s4 build/manual >/dev/null
cp build/manual/screen-*.png docs/img/
python3 -c 'import sys; from PIL import Image; [Image.open(f).save(f, optimize=True) for f in sys.argv[1:]]' docs/img/screen-*.png
python3 tools/gen_manual_figures.py docs/img/screen-mono.png
ls docs/img
