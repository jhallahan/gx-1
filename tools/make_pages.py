#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the SEQ-4 site (a static folder: GitHub Pages or any web server serves it as it is):

  index.html                  what SEQ-4 is, and the ways in: try, install, manual, download, source
  img/                        screenshots (docs/img)
  emu/                        SEQ-4 in the browser (build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/seq4_installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the manual (tools/manual_html.py, from docs/MANUAL.md)
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
<title>SEQ-4</title>
<meta name="description" content="Firmware that turns the M-VAVE FM-1 into four MIDI sequencers: six modes (Mono, Poly, Chord, Multitrack, Stochastic, Matriceal), 128 steps, patterns and songs, LFOs, mod lanes, generators, a harmonizer, on four USB MIDI ports.">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Crect width='16' height='16' rx='3' fill='%230c0c10'/%3E%3Crect x='2' y='2' width='5' height='5' rx='1' fill='%2300beff'/%3E%3Crect x='9' y='2' width='5' height='5' rx='1' fill='%23ff46be'/%3E%3Crect x='2' y='9' width='5' height='5' rx='1' fill='%23ffaa00'/%3E%3Crect x='9' y='9' width='5' height='5' rx='1' fill='%2350e678'/%3E%3C/svg%3E">
<style>
/* SEQ-4's colours: lit pads on black, the four sequencers' cyan, magenta, amber and green */
:root { --paper: #0c0c10; --sheet: #1c1d24; --ink: #eceef6; --muted: #9496a4; --rule: #2a2b34;
        --s1: #00beff; --s2: #ff46be; --s3: #ffaa00; --s4: #50e678; color-scheme: dark; }
body { background: var(--paper); color: var(--ink); margin: 0; padding: 40px 16px 64px; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; }
main { max-width: 46rem; margin: 0 auto; }
h1 { font: 700 3rem/1.1 "Helvetica Neue", Helvetica, Arial, sans-serif; letter-spacing: .04em; margin: 0; }
.pads { display: grid; grid-template-columns: repeat(16, 1fr); gap: 4px; margin: 1rem 0 0; max-width: 26rem; }
.pads span { aspect-ratio: 1; border-radius: 3px; background: #22242c; }
.sub { color: var(--muted); margin: .9rem 0 1.4rem; font-size: 1.1rem; }
.lede { font-size: 1.12rem; }
.shots { display: flex; flex-wrap: wrap; gap: 12px; margin: 1.6rem 0; }
.shots img { width: calc((100% - 24px) / 3); min-width: 150px; height: auto; image-rendering: pixelated; border-radius: 6px; }
h2 { font-size: 1.15rem; margin: 2.2rem 0 .6rem; padding-top: .6rem; border-top: 1px solid var(--rule); }
.ways { display: grid; grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr)); gap: 12px; margin: 1.6rem 0; }
.ways a { display: block; padding: .8rem 1rem; background: var(--sheet); border: 1px solid var(--rule); border-radius: 8px; text-decoration: none; color: var(--ink); }
.ways a:first-child { background: var(--s1); color: #04121a; }
.ways a strong { display: block; } .ways a span { display: block; font-size: .9rem; opacity: .8; }
.ways a:hover, .ways a:focus-visible { border-color: var(--s1); outline: none; }
.v { font-style: normal; font-weight: 400; opacity: .8; }
table.modes { border-collapse: collapse; width: 100%; font-size: .95rem; }
table.modes td { padding: .35rem .5rem; border-bottom: 1px solid var(--rule); vertical-align: top; }
table.modes td:first-child { font-weight: 700; white-space: nowrap; }
.small { color: var(--muted); font-size: .9rem; }
a { color: var(--ink); text-decoration-color: var(--s1); text-decoration-thickness: 2px; text-underline-offset: 3px; }
code { font-size: .88em; }
</style>
</head>
<body>
<main>
<h1>SEQ-4</h1>
<div class="pads" aria-hidden="true">__PADS__</div>
<p class="sub">Four MIDI sequencers for the M-VAVE FM-1</p>
<p class="lede">SEQ-4 turns the FM-1 into four sequencers for the synths, drum machines and software around it. Each
plays on its own channel and USB port (four ports: 64 channels) in one of six modes. The screen is the grid, sixteen
steps by eight rows of lit pads; the white keys are the steps, the black keys the rows. Patterns and songs, LFOs and
mod lanes, Euclidean and drum generators, a harmonizer, recording and capture.</p>
<div class="shots">
  <img src="img/screen-mono.png" width="240" height="240" alt="SEQ-4's grid: a bass line in A minor, a step on each row of the scale">
  <img src="img/screen-chord.png" width="240" height="240" alt="SEQ-4's grid in Chord mode: each chord's root bright, its other notes softer">
  <img src="img/screen-drums.png" width="240" height="240" alt="SEQ-4's grid in Multitrack mode: eight drum tracks">
</div>
<nav class="ways" aria-label="Get SEQ-4">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, playing your synths through Web MIDI. No FM-1 needed.</span></a>
  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="manual/"><strong>Manual</strong><span>Getting started in ten steps, then every key, mode and menu.</span></a>
  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="source/"><strong>Source</strong><span>GPL-3.0. Built on Felucca by Hügelton Instruments, by way of X0X, AMB-1, IND-1, LCD-4 and CRT-4.</span></a>
</nav>

<h2>The six modes</h2>
<table class="modes">
<tr><td>Mono</td><td>A melody, a note a step on a scale's rows: ties, legato, glide, octaves.</td></tr>
<tr><td>Poly</td><td>Up to seven notes a step: play a hand on the keyboard, tap a step and it is there.</td></tr>
<tr><td>Chord</td><td>A chord a step: its type, voicing, spread, strum and bass; its own arpeggiator.</td></tr>
<tr><td>Multitrack</td><td>Eight tracks of drums, each its own note, channel, length and division; Euclid and a drum generator on Grids' map.</td></tr>
<tr><td>Stochastic</td><td>Chances for each note, octave and length: it improvises.</td></tr>
<tr><td>Matriceal</td><td>Lanes of triggers, notes, intervals, pulses, velocity, gate and more, each its own length and speed.</td></tr>
</table>
<p>Every step has its velocity, gate, octave, glide, timing, repeats and a condition (a chance, A:B, first, fill and
more). Up to 128 steps, sixteen patterns a sequencer, ten songs. Live and step recording, MIDI capture, undo.
Random perform, a random generator, two LFOs and mod lanes a sequencer, MIDI mappings with learn, mutes, fills,
snapshots, and MIDI clock in and out. The FM-1 itself only clicks the metronome: SEQ-4 is a controller.</p>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or
M-VAVE's updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>.
Many of SEQ-4's ideas come from the OXI One sequencer by OXI Instruments; the drum generator uses Grids' map by
Emilie Gillet (Mutable Instruments). M-VAVE and FM-1 are trademarks of their owners, as is OXI One. SEQ-4 is
independent and not affiliated with any of them.</p>
</main>
</body>
</html>
"""

SOURCE = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>SEQ-4 source</title>
<style>body { background: #161619; color: #eae8e0; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; margin: 48px 16px; }
main { max-width: 42rem; margin: 0 auto; } a { color: #eae8e0; text-decoration-color: #38c4e8; text-decoration-thickness: 2px; }</style></head>
<body><main><h1>SEQ-4 source</h1>
<p>SEQ-4 is free software under the GNU General Public License, version 3 only. The complete source of v__VERSION__:
<a href="__TAR__">__TAR__</a>.__REPOLINE__</p>
<p><a href="../">Back to SEQ-4</a></p></main></body></html>
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
    for d in ("install", "manual", "firmware", "source"):
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
    # the manual (a complete page; its images beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    # the landing page and its screenshots
    shutil.copytree(SRC / "docs" / "img", out / "img")
    pads = "".join(                                # four rows of lit pads, the sequencers' colours
        f'<span style="background:var(--s{r + 1})"></span>' if ((c * 5 + r * 3) % 7) < 2 + r % 2 else "<span></span>"
        for r in range(4) for c in range(16))
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__PADS__", pads),
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
    print(f"site: {out}: index.html, install/ ({product}), manual/, emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
