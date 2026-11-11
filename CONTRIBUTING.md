# Make it yours

GX-1 is free software (GPL-3.0), built on SEQ-4. You're welcome to fork it, change it, rename it and release
your own version; no need to ask.

**GX-1 doesn't take outside contributions.** Pull requests aren't accepted, however small, so please don't
send one: it will be closed unread. Bug reports and ideas are welcome as
[issues](https://github.com/jhallahan/gx-1/issues/new/choose). For a conventional sequencer, or to send
changes to the sequencer underneath, go to [SEQ-4](https://github.com/devrvega/SEQ-4) itself.

## What the licence asks

If you give someone a firmware built from this code (a release, a `.fwsc` in a chat, a page with an
installer), give them its complete source under the same licence, and keep the copyright notices and
`LICENSE` that are in the files. That's all. The release workflow and the site generator already do it:
every release's tag is its source, and the site carries a tarball of it.

## Build it, try it, test it

Everything runs on GitHub: a fork builds and tests every push (**Actions → Build and test**), and the firmware
is attached to the run as an artifact to download. On your own machine:

| | |
|---|---|
| `tests/run_tests.sh` | the tests (the platform, the flash, the app in a simulator, the browser build if Emscripten is there) |
| `./build.sh` | the firmware, `build/gx1.fwsc` (needs the JieLi toolchain and three SDK files; see [BUILDING.md](BUILDING.md)) |
| `build/host/seq4_host SCRIPT OUT` | the whole app on your computer from a script, with the screen as PNGs and the MIDI as a timed log (`tests/scenarios/*.s4`) |
| `web/emu/build.sh` | the browser emulator (needs Emscripten) |
| `python3 tools/fm1_install.py build/gx1.fwsc` | install it over USB |
| `python3 tools/fm1_status.py load` | the engine's load on the FM-1 itself, the screen's, the MIDI it sends |

On a Mac the build runs the JieLi tools in Docker; on Linux x86-64 (and on GitHub) it runs them directly.
`tools/get_toolchain.sh` and `tools/get_sdk_files.sh` fetch what it needs.

## Where things are

| | |
|---|---|
| `firmware/src/seq/seq4.h` | the data (a sequencer, the project, songs) and the engine's calls |
| `firmware/src/seq/seq4.c` | the engine: its clock, commands, MIDI in and out, transport, boundaries (one unit, with the files below) |
| `firmware/src/seq/s4_play.c` | playing the steps of each mode, conditions, repeats, ties; patterns changing, mutes, songs |
| `firmware/src/seq/s4_music.c` | scales, chords, voicings, the harmonizer |
| `firmware/src/seq/s4_mod.c`, `s4_arp.c`, `s4_rec.c` | LFOs, mod lanes, mappings; the arpeggiators; recording and capture |
| `firmware/src/seq/s4_edit.c` | defaults, undo, the edits (rotate, transpose...), the generators, patterns as bytes |
| `firmware/src/seq/grids_map.h` | Grids' drum map (Emilie Gillet, GPL-3.0-or-later) |
| `firmware/src/app/ui.c`, `ui_input.c`, `ui_draw.c` | the menus and step edits; the keys, buttons and lights; the screen |
| `firmware/src/app/project.c` | the project, patterns and songs in flash; the demo |
| `firmware/src/logstore.c` | the flash log the patterns live in |
| `firmware/src/app/main_fm1.c` | the FM-1: boot, the engine's tick in the audio interrupt, the click, the status SysEx |
| `firmware/hal/`, `firmware/loader/`, `firmware/src/{usb,storage,ota}.c` | the FM-1 platform (Felucca's) |
| `host/`, `tests/` | the simulator and the tests |
| `web/`, `tools/make_pages.py` | the emulator, the installer, the site |

## Your own firmware, from this one

To release it under your own name, so that it and SEQ-4 can live side by side (and on the
[FM-1 firmware hub](https://thefirmwarehub.com/)):

1. **The name.** Search for `SEQ-4` and change what you want: the start screen (`firmware/src/app/main_fm1.c`),
   the USB names (`STR1` and `STR2` in `firmware/src/usb.c`), the docs and the site.
2. **The identity**, what the FM-1 reports and the installers check: `PRODUCT` and the release prefix in
   `tools/build.py`, and the `FELUCCA_ID` default in `firmware/src/main.c`. Pick a prefix nobody uses (SEQ-4
   is `FM-1_1…`); the hub's [catalogue](https://thefirmwarehub.com/catalog.json) lists the ones in use.
3. **The saved data's tags**, `PROJ_MAGIC` in `firmware/src/app/app.h` and the log's `LS_SMAGIC` in
   `firmware/src/logstore.c`, so that your firmware and SEQ-4 never read each other's saves.
4. **Releases:** push a tag `vX.Y.Z`; the Release workflow builds and publishes it. For the site, set
   **Settings → Pages → Source** to **GitHub Actions**; the Pages workflow then publishes it after each
   release.

## Notes on the code

A new mode: its steps in the union at the end of `s4_seq_t` (`seq4.h`), its playing in `s4_play.c`
(`trk_run`), its packing in `s4_edit.c`, its grid in `ui_draw.c` (`grid_state`) and its keys in `ui.c`
(`tap_step`, `row_step`).

Some habits of the code: C99, `float` only (the FM-1 has no double-precision hardware, and the build fails if
a `double` gets in), no libm (`fastmath.h` instead), and nothing slow in the engine's tick: it runs every
0.73 ms in an interrupt, and its worst case is measured (the bench builds). The comments say what a thing is for,
in plain words.
