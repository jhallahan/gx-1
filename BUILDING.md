# Building SEQ-4

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `gx1.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `gx1.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- A C compiler for the computer itself (`cc`: Xcode's command line tools on a Mac; gcc on Linux), for the
  tests and the simulator.
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- Three files of the JieLi AC79 SDK (Apache-2.0): `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin` and
  `cfg/eq_cfg_hw.bin`. They are not part of this tree; this fetches just them, from JieLi's repository or a
  GitHub mirror of it, and keeps them only if their SHA-256 is the one the build expects:

  ```
  tools/get_sdk_files.sh            # into ~/fw-AC79_AIoT_SDK
  ```

  (or the whole SDK: `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13
  https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK`)

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1` makes a release build; the package is `build/seq4-0.1.fwsc`.

`./build.sh --bench` makes a development build (`build/gx1-bench.fwsc`) that also takes test projects over
SysEx, to measure the FM-1 itself. Every build answers the status query: send `F0 7D 53 34 01 F7` and the
FM-1 replies `F0 7D 53 34 01` with these, each as five 7-bit groups:
- the engine's load (%), its slowest tick (µs, of the 725 µs between ticks), ticks late, ticks
- the bench flag, the slowest tick since `03`, the core's clock (when its counter runs)
- the screen's frames; playing, following a clock, recording; MIDI messages out; dropped (the engine's queue
  in the low 16 bits, the USB ring's above); MIDI messages in
- the screen's time (µs, summed) and its slowest frame, the tempo (BPM × 10), the longest gap between two
  ticks, the store's bytes in use and its room, the USB ring's high water, USB up and suspended

`F0 7D 53 34 03 F7` clears the slowest and the late; `04 s` leaves the screen undrawn for s seconds; `05`
reports the store (flash found, the log's and each object's last save, which patterns are saved, its bytes);
`07` what plays (each sequencer's step, pattern, mode and mute; the bar, the beat). A bench build also takes:

| SysEx | What it does |
|---|---|
| `F0 7D 53 34 02 k F7` | Loads test project k. 0: all four as busy as they get (eight drum tracks with repeats and eight mod lanes, seven-note chords on every step, strummed and arpeggiated chords, a 1/32 line under two 200 Hz LFOs, 200 BPM, clock on all ports). 1: the demo. 2: a sixteenth line on port A, channel 1, at 120 BPM, to time. |
| `06 p` | Plays (1) or stops (0). |
| `08` | The demo, saved. |
| `0A` | Times each part of a screen frame (ticks of 24 MHz), and the wait for the LCD's transfers. |
| `0C v s` | A view (0 the grid, 1 the keyboard, 2 the mod lanes, 3 the patterns, 4 the songs) and a sequencer, as if picked: to time the screen. |

On macOS with podman instead of Docker, put a `docker` script that runs `exec podman "$@"` first
on your PATH. `EN_JOBS` (default 4) limits parallel compiles: a podman machine drops
connections when many containers start at once.

Build option: `EN_CDC=1` adds Felucca's USB serial function (off by default: one plain
MIDI interface).

The build also generates `build/gen/felucca_font.h`, the font. It fails if a soft-double routine is linked
(a `double` crept in) and checks the image, RAM and pool sizes. The sequencer (`firmware/src/seq/seq4.c`
and the files it includes) is compiled on its own at -O2; the rest is one unit at -Os.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the maths library against libm; the panel's encoder decoding and the TRS
jack's MIDI parser; flash storage (the settings' objects, and the patterns' log with a cut at every point of
a save); Felucca's update-path tests against `build/gx1.fwsc`; and the whole app in the simulator
(`tests/scenarios/*.s4`: the steps, the six modes, recording and capture, patterns and persistence, songs,
MIDI in and the transport CCs, the menus), with its screenshots and MIDI logs in `build/scenarios/`; the
browser build too when Emscripten is there.

`host/build_host.sh` builds the simulator alone; `build/host/seq4_host SCRIPT OUTDIR` runs one script (the
command list is at the top of `host/seq4_host.c`). The simulator ticks the engine as the FM-1 does (every
32 samples), writes every MIDI message with its time to the microsecond (`OUTDIR/midi_out.txt`), and keeps
its flash across a simulated restart. `bench K` loads the bench projects.

- `tools/manual_shots.sh`: the manual's screenshots into `docs/img`.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/gx1.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for Felucca's update
mode. If the FM-1 no longer starts but reaches the chip's update mode (4C4A:8057 on USB),
`bash tools/fm1_rescue.sh` puts stock firmware back from a Mac; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## On GitHub: builds, releases, the site

Three workflows in `.github/workflows/` (they work the same in a fork):

- **Build and test** (every push and pull request): the toolchain and the SDK files fetched (and cached), the
  firmware built, `tests/run_tests.sh` run (with the browser build), the package kept as an artifact.
- **Release** (a tag `vX.Y.Z`): the tests, then `./build.sh --release X.Y.Z` (identity `FM-1_1XXYYZZ`, the
  version on the FM-1's start screen), and a GitHub release with `seq4-X.Y.Z.fwsc`, its notes taken from
  `CHANGELOG.md`.
- **Pages** (after a release, or by hand): the site for the latest release on GitHub Pages: the landing page,
  the browser emulator, the web installer with the release's package, the manual, and the complete source as
  a tarball (as the GPL asks). In a fork, set **Settings → Pages → Source** to **GitHub Actions** first.

To release: add the version's section to `CHANGELOG.md`, then `git tag vX.Y.Z && git push origin vX.Y.Z`.

The same by hand:

1. `./build.sh --release X.Y.Z` (the identity, FM-1_1XXYYZZ, is what the installer checks; CRT-4's is
   FM-1_2..., Polly's FM-1_3..., LCD-4's FM-1_4..., IND-1's FM-1_5..., CTL-1's FM-1_6..., AMB-1's FM-1_7...,
   FiMba's FM-1_8..., X0X's FM-1_9...).
2. `web/emu/build.sh` (needs Emscripten) for the browser emulator.
3. `[SEQ4_REPO=https://...] python3 tools/make_pages.py build/seq4-X.Y.Z.fwsc X.Y.Z OUT_DIR`: the site in a
   static folder, for any web server. The web installer needs it served over HTTPS (Web MIDI). The
   manual's source is `docs/MANUAL.md` (`tools/manual_html.py` turns it into the page).
4. `tools/readme_gif.py` remakes the README's animation, `tools/manual_shots.sh` the manual's screenshots.

## On the FM-1: how it's doing

`python3 tools/fm1_status.py load` asks a connected FM-1 running SEQ-4 for its load, the screen's frames and
the MIDI it sends; `play` for what it's playing, `store` for what's saved, `timing` to measure how evenly its
notes arrive (it needs a bench build: it loads the timing line and plays it). Apart from `timing`, it only
asks; it never changes anything on the FM-1.
