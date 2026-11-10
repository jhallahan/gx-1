#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the GX-1 site (a static folder: GitHub Pages or any web server serves it as it is):

  index.html                  what GX-1 is, and the ways in: install, manual, download, try, source
  img/                        screenshots (docs/img)
  emu/                        SEQ-4 in the browser (build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/seq4_installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the GX-1 manual (tools/manual_html.py, from docs/GX1_MANUAL.md)
  seq4-manual/index.html, img/  the SEQ-4 manual underneath (from docs/MANUAL.md)
  firmware/gx1-VERSION.fwsc   the package the installer writes; also the download
  source/                     the complete source (GPL-3.0: whoever gets the firmware gets the source),
                              as a tarball of this tree; and SEQ4_REPO, if set, the repository's address

  [SEQ4_REPO=https://...] tools/make_pages.py build/gx1-0.1.fwsc 0.1 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_1XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

REPO = os.environ.get("SEQ4_REPO", "")

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GX-1</title>
<meta name="description" content="Genre-aware generative MIDI firmware for the M-VAVE FM-1: twenty styles in five families compose drums, sub, chords and a lead you can mutate, lock, re-roll and arrange in song parts. Built on SEQ-4.">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Crect width='16' height='16' rx='3' fill='%23111114'/%3E%3Crect x='2' y='3' width='12' height='2' rx='1' fill='%23f2b44e'/%3E%3Crect x='2' y='6' width='9' height='2' rx='1' fill='%23ff6f61'/%3E%3Crect x='2' y='9' width='11' height='2' rx='1' fill='%235fd6a0'/%3E%3Crect x='2' y='12' width='7' height='2' rx='1' fill='%23a08cff'/%3E%3C/svg%3E">
<style>
/* GX-1's colours, as the GEN page: the four tracks' amber, coral, green and violet on near-black */
:root { --paper: #111114; --sheet: #1e1e23; --ink: #ececf0; --muted: #9a9aa4; --rule: #2e2e34;
        --t1: #f2b44e; --t2: #ff6f61; --t3: #5fd6a0; --t4: #a08cff; color-scheme: dark; }
body { background: var(--paper); color: var(--ink); margin: 0; padding: 40px 16px 64px; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; }
main { max-width: 46rem; margin: 0 auto; }
h1 { font: 700 3rem/1.1 "Helvetica Neue", Helvetica, Arial, sans-serif; letter-spacing: .04em; margin: 0; }
.bars { display: grid; gap: 5px; margin: 1rem 0 0; max-width: 26rem; }
.bars div { display: grid; grid-template-columns: repeat(16, 1fr); gap: 3px; height: 10px; }
.bars span { border-radius: 2px; background: #24242a; }
.sub { color: var(--muted); margin: .9rem 0 1.4rem; font-size: 1.1rem; }
.lede { font-size: 1.12rem; }
.shots { display: flex; flex-wrap: wrap; gap: 12px; margin: 1.6rem 0; }
.shots img { width: calc((100% - 24px) / 3); min-width: 150px; height: auto; image-rendering: pixelated; border-radius: 6px; }
h2 { font-size: 1.15rem; margin: 2.2rem 0 .6rem; padding-top: .6rem; border-top: 1px solid var(--rule); }
.ways { display: grid; grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr)); gap: 12px; margin: 1.6rem 0; }
.ways a { display: block; padding: .8rem 1rem; background: var(--sheet); border: 1px solid var(--rule); border-radius: 8px; text-decoration: none; color: var(--ink); }
.ways a:first-child { background: var(--t1); color: #1a1206; }
.ways a strong { display: block; } .ways a span { display: block; font-size: .9rem; opacity: .8; }
.ways a:hover, .ways a:focus-visible { border-color: var(--t1); outline: none; }
.v { font-style: normal; font-weight: 400; opacity: .8; }
table.modes { border-collapse: collapse; width: 100%; font-size: .95rem; }
table.modes td { padding: .35rem .5rem; border-bottom: 1px solid var(--rule); vertical-align: top; }
table.modes td:first-child { font-weight: 700; white-space: nowrap; }
.small { color: var(--muted); font-size: .9rem; }
ol { padding-left: 1.3rem; }
a { color: var(--ink); text-decoration-color: var(--t1); text-decoration-thickness: 2px; text-underline-offset: 3px; }
code { font-size: .88em; }
</style>
</head>
<body>
<main>
<h1>GX-1</h1>
<div class="bars" aria-hidden="true">__BARS__</div>
<p class="sub">Generative, genre-aware MIDI for the M-VAVE FM-1</p>
<p class="lede">Pick a style (Liquid, Jungle, Dub techno, Acid, Deep house, R&B and nineteen more), press a button, and GX-1
composes four tracks that fit together: drums, a sub bass, chords and a lead. Then steer it: mutate a track, lock the ones
you like, re-roll the rest, turn the macros, move between song parts, change key. It plays out over USB MIDI to your DAW,
a Tonverk or any synth, and SEQ-4's full sequencer is underneath for editing by hand.</p>
<p class="lede">Built on <a href="https://github.com/devrvega/SEQ-4">SEQ-4</a> by devrvega, which gave it a solid basis. Want a conventional sequencer for the FM-1? Use SEQ-4 itself.</p>
<div class="shots">
  <img src="img/gx1-gen.png" width="240" height="240" alt="GEN: the four tracks, the idea number, the macros and the selected track's steps">
  <img src="img/gx1-track.png" width="240" height="240" alt="TRACK: the drum lanes, the track's own macros and MUTATE">
  <img src="img/gx1-arrange.png" width="240" height="240" alt="ARRANGE: the energy of the six parts and the parts on W1 to W6">
</div>
<nav class="ways" aria-label="Get GX-1">
  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="manual/"><strong>Manual</strong><span>Five minutes to your first track, then every control, style and part.</span></a>
  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 fm1_install.py __PKG__</code></span></a>
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, playing your synths through Web MIDI.</span></a>
  <a href="seq4-manual/"><strong>SEQ-4 manual</strong><span>The sequencer underneath: the grid, modes, patterns, LFOs and menus.</span></a>
  <a href="source/"><strong>Source</strong><span>GPL-3.0. Built on SEQ-4, Felucca by Hügelton Instruments, X0X, AMB-1, IND-1, LCD-4 and CRT-4.</span></a>
</nav>

<h2>Start in five steps</h2>
<ol>
<li>Install (above) and connect the FM-1 to your computer by USB.</li>
<li>In your DAW, take MIDI from <code>GX-1 A</code>: drums on channels 1 and 2, sub on 5, chords on 7, lead on 8.</li>
<li>Press <strong>PLAY</strong>. Press <strong>FX</strong> for a new idea.</li>
<li>Turn <strong>ALGORITHM</strong> for the family; <strong>GLO + ALGORITHM</strong> for the style.</li>
<li>Black keys 1–4 select a track: <strong>SEL</strong> mutates it, <strong>ENV</strong> locks it. <strong>GLO + W9–W14</strong>: the song parts.</li>
</ol>

<h2>Twenty styles in five families</h2>
<table class="modes">
<tr><td>DNB</td><td>Deep DnB, Liquid, Rollers, Jungle, Halftime, Autonomic</td></tr>
<tr><td>Breaks &amp; bass</td><td>Breaks, UK garage, Electro, Dubstep</td></tr>
<tr><td>Techno</td><td>Minimal, Dub techno, Acid, Detroit, Hard</td></tr>
<tr><td>House</td><td>Deep house, Lo-fi house</td></tr>
<tr><td>Leftfield</td><td>IDM, Downtempo, Ambient</td></tr>
</table>
<p>Each style has its own drum language, scale, chord progressions, bass and lead behaviour, tempo range and six song
parts (intro, main, variation, break, build, drop). Eight macros (Density, Sync, Space, Groove, and Human, Tension, Complex,
Evolve with GLO), mutation by aspect, locks, re-roll, undo and redo. The same style and seed always give the same music.</p>
<p class="small">The FM-1 makes no sound of its own with GX-1: it is a controller. To go back to the official firmware, use
the installer's "Back to the stock firmware", or M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. GX-1 is built on SEQ-4 by devrvega. M-VAVE, FM-1,
Elektron and Tonverk are trademarks of their owners. GX-1 is independent and not affiliated with any of them.</p>
</main>
</body>
</html>
"""

SOURCE = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>GX-1 source</title>
<style>body { background: #161619; color: #eae8e0; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; margin: 48px 16px; }
main { max-width: 42rem; margin: 0 auto; } a { color: #eae8e0; text-decoration-color: #38c4e8; text-decoration-thickness: 2px; }</style></head>
<body><main><h1>GX-1 source</h1>
<p>GX-1 (built on SEQ-4) is free software under the GNU General Public License, version 3 only. The complete source of v__VERSION__:
<a href="__TAR__">__TAR__</a>.__REPOLINE__</p>
<p><a href="../">Back to GX-1</a></p></main></body></html>
"""


def source_tarball(dest, version):
    """the tree as it is (no build output, no caches): what the GPL asks to give with the firmware"""
    name = f"seq-4-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}-src"
    skip = {"build", "__pycache__", ".git", ".DS_Store", ".serena"}

    def keep(ti):
        return None if set(Path(ti.name).parts) & skip else ti
    with tarfile.open(dest / f"{name}.tar.gz", "w:gz") as t:
        t.add(SRC, arcname=name, filter=keep)
    return f"{name}.tar.gz"


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_1\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"gx1-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "seq4-manual", "firmware", "source"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the installer
    page = (SRC / "web" / "seq4_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"seq4_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    # the manuals (complete pages; their images beside them): GX-1's, and SEQ-4's underneath
    for md, d in (("GX1_MANUAL.md", "manual"), ("MANUAL.md", "seq4-manual")):
        subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / md),
                        str(out / d / "index.html")], check=True, capture_output=True)
        shutil.copytree(SRC / "docs" / "img", out / d / "img")
    # the landing page and its screenshots
    shutil.copytree(SRC / "docs" / "img", out / "img")
    bars = "".join(                                # the GEN page's four tracks: a bar of hits each, in their colours
        "<div>" + "".join(f'<span style="background:var(--t{r + 1})"></span>' if hit else "<span></span>"
                          for hit in row) + "</div>"
        for r, row in enumerate((
            [c % 4 == 0 or c in (10, 14) for c in range(16)],              # drums
            [c in (0, 3, 6, 10, 11) for c in range(16)],                   # sub
            [c in (0, 1, 2, 8, 9, 10) for c in range(16)],                 # chords
            [c in (2, 5, 7, 12, 15) for c in range(16)])))                 # upper
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__BARS__", bars),
                                    encoding="utf-8")
    # the source, for the GPL
    tar = source_tarball(out / "source", version)
    repo = f' The repository: <a href="{html.escape(REPO)}">{html.escape(REPO)}</a>.' if REPO else ""
    (out / "source" / "index.html").write_text(SOURCE.replace("__VERSION__", html.escape(version)).replace("__TAR__", tar)
                                               .replace("__REPOLINE__", repo), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "seq4.wasm").exists():
        raise SystemExit("no build/emu/seq4.wasm: run web/emu/build.sh (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), manual/, seq4-manual/, emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
