# SEQ-4 licensing

SEQ-4 is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on CRT-4, LCD-4, IND-1 and AMB-1, which are built on
Felucca's platform layer (through X0X and FoMni), and keeps their licence. If you distribute SEQ-4, or
firmware derived from it, you must give your recipients its complete corresponding source under the same
licence (`tools/make_pages.py` puts a tarball of the source on the site it makes, next to the firmware).

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x), FoMni and [AMB-1](https://github.com/charlesvestal/fm1-amb), then IND-1, LCD-4, CRT-4 and SEQ-4 (USB: four MIDI ports) | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, the frame of `firmware/src/app/{project,ui}.c` (input, knobs, help cards, lights), `host/seq4_host.c`, `web/emu/`, `web/seq4_installer.html`, `tools/{make_pages,manual_html,fm1_status}.py` | adapted from CRT-4, LCD-4, IND-1 and AMB-1 (themselves from X0X, from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| `firmware/src/seq/grids_map.h`: the drum generator's map (25 nodes of 96 values, the 5 × 5 grid) and the way it is read | [Grids](https://github.com/pichenettes/eurorack/tree/master/grids) by Emilie Gillet, Mutable Instruments: `grids/resources.cc` and `grids/pattern_generator.cc` | GPL-3.0-or-later (here under GPL-3.0-only, as it allows) |
| The ideas of many of SEQ-4's features and ways of working (the six modes, the conditions, the random perform and generators, the arranger, the harmonizer, MIDI capture, mappings) | the [OXI One](https://oxiinstruments.com/oxi-one/) sequencer by OXI Instruments, as its user manual describes them; no code, text, graphics or data from it | credit only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else: the sequencer engine (`firmware/src/seq/`), the log store (`firmware/src/logstore.c`), the UI and its screen (`firmware/src/app/ui*.c`), the demo, `tests/` | SEQ-4 | GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the screen's face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks
of their respective owners; "OXI One" and "OXI Instruments" are OXI Instruments'; "Mutable Instruments" and
"Grids" are Mutable Instruments'. SEQ-4 is independent firmware; it is not affiliated with, endorsed by or
supported by any of them. Their names are used only to credit where ideas and code come from.

## Radio

SEQ-4 never enables the Bluetooth / Wi-Fi radio of the hardware.
