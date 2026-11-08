#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drive GX-1's genre engine on an FM-1 over USB (SysEx), until the panel's GEN page lands.

    pip3 install mido python-rtmidi
    python3 tools/gx_send.py new breaks 42 [PART]   # a new idea: deep_dnb | breaks | autonomic; PART 0..5
    python3 tools/gx_send.py part 4                 # INTRO 0, MAIN A 1, MAIN B 2, BREAK 3, BUILD 4, DROP 5
    python3 tools/gx_send.py compose 7              # new idea, locked parts kept
    python3 tools/gx_send.py lock 0 1               # role 0 drums, 1 sub, 2 chords, 3 upper; 1 lock, 0 unlock
    python3 tools/gx_send.py mutate 1 15 40         # role, aspects (1 rhythm 2 pitch 4 velocity 8 length), intensity
    python3 tools/gx_send.py macro 0 90             # 0 density 1 sync 2 space 3 groove 4 humanise 7 evolution
    python3 tools/gx_send.py undo | redo
    python3 tools/gx_send.py map tonverk | gm

The FM-1 answers with the number of events it staged; the parts start at the next boundary while playing.
"""
import sys
import time

GENRES = {"deep_dnb": 1, "breaks": 2, "autonomic": 3}
HDR = [0x7D, 0x53, 0x34]


def seed7(v):
    v &= 0xFFFFFFFF
    return [(v >> (7 * k)) & 0x7F for k in range(5)]


def message(args):
    op = args[0]
    if op == "new":
        body = [0x10, GENRES[args[1]], *seed7(int(args[2]))] + ([int(args[3])] if len(args) > 3 else [])
    elif op == "part":
        body = [0x11, int(args[1])]
    elif op == "compose":
        body = [0x12, *seed7(int(args[1]))]
    elif op == "lock":
        body = [0x13, int(args[1]), int(args[2])]
    elif op == "mutate":
        body = [0x14, int(args[1]), int(args[2]), int(args[3])]
    elif op == "undo":
        body = [0x15]
    elif op == "redo":
        body = [0x16]
    elif op == "macro":
        body = [0x17, int(args[1]), int(args[2])]
    elif op == "map":
        body = [0x18, 1 if args[1] == "gm" else 0]
    else:
        raise ValueError(op)
    return HDR + body


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    try:
        data = message(sys.argv[1:])
    except (KeyError, IndexError, ValueError):
        sys.exit(__doc__)
    import mido
    outs = [n for n in mido.get_output_names() if "GX-1" in n or "SEQ-4" in n or "FM-1" in n]
    ins = [n for n in mido.get_input_names() if "GX-1" in n or "SEQ-4" in n or "FM-1" in n]
    if not outs:
        sys.exit("gx_send: no GX-1 / FM-1 MIDI port found (is it plugged in and running GX-1?)")
    with mido.open_output(outs[0]) as out, (mido.open_input(ins[0]) if ins else None) as inp:
        out.send(mido.Message("sysex", data=data))
        t0 = time.time()
        while inp and time.time() - t0 < 1.0:
            for m in inp.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [0x10]:
                    v = sum(b << (7 * k) for k, b in enumerate(m.data[4:9]))
                    print("failed" if v == 0x7FFFFFFF else f"staged {v} events")
                    return
            time.sleep(0.01)
        print("sent (no answer heard)")


if __name__ == "__main__":
    main()
