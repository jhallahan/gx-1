#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ask a connected FM-1 running SEQ-4 how it's doing, over USB-MIDI (SEQ-4's SysEx, F0 7D 53 34 .. F7,
described in firmware/src/app/main_fm1.c and BUILDING.md). Needs mido with python-rtmidi. Apart from
`timing`, nothing it sends changes anything on the FM-1.

  tools/fm1_status.py load [SECONDS]   the engine's load: %, its slowest tick, ticks late; the screen's frames
                                       a second and their time; MIDI out a second, dropped; over SECONDS
                                       (default 5); play something meanwhile
  tools/fm1_status.py play             what plays: each sequencer's step, pattern, mode, mute; the bar
  tools/fm1_status.py store            what's saved: the patterns, the store's bytes in use
  tools/fm1_status.py timing [SECONDS] (a bench build: ./build.sh --bench) loads the timing line (sixteenths
                                       at 120 BPM on port A, channel 1), plays it, and measures how evenly its
                                       notes arrive here; it replaces what the FM-1 was playing (not saved)

The engine ticks every 725 us (32 samples at 44.1 kHz); the FM-1 keeps up while the slowest tick stays
under that."""
import os
import sys
import time

import mido

HDR = [0x7D, 0x53, 0x34]
NAMES = ["cpu", "worst_us", "late", "ticks", "bench", "tick_max_us", "mhz", "frames", "flags", "events",
         "drops", "midi_in", "ui_us", "ui_us_max", "tempo", "gap_max_us", "log_live", "log_cap", "usb_hw", "usb"]
MODES = ["Mono", "Poly", "Chord", "Multitrack", "Stochastic", "Matriceal"]


def r35(b, i):
    return sum(b[i + k] << (7 * k) for k in range(5))


class Dev:
    def __init__(self):
        name = next((n for n in mido.get_output_names() if "FM-1" in n or "SEQ-4" in n), None)
        if not name:
            sys.exit(f"fm1_status: no FM-1 MIDI port (ports: {mido.get_output_names()})")
        self.i = mido.open_input(name)
        self.o = mido.open_output(name)
        time.sleep(0.3)
        self.ask(7, timeout=1.0, must=False)     # (the first message after opening can go astray)

    def drain(self, secs=0.1):
        end = time.time() + secs
        while self.i.poll() is not None and time.time() < end:
            pass

    def ask(self, cmd, args=(), reply=True, timeout=2.0, must=True):
        self.drain()
        self.o.send(mido.Message("sysex", data=HDR + [cmd] + list(args)))
        if not reply:
            time.sleep(0.15)                     # the FM-1 takes one SysEx at a time
            return None
        end = time.time() + timeout
        while time.time() < end:
            m = self.i.poll()
            if m is not None and m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                return list(m.data[4:])
            if m is None:
                time.sleep(0.002)
        if not must:
            return None
        sys.exit("fm1_status: no answer (is it SEQ-4, and a version that answers this?)")

    def status(self):
        d = self.ask(1)
        return {n: r35(d, 5 * k) for k, n in enumerate(NAMES) if 5 * k + 5 <= len(d)}


def load(dev, secs):
    dev.ask(3, reply=False)
    s0 = dev.status()
    time.sleep(secs)
    s1 = dev.status()
    fr = max(s1["frames"] - s0["frames"], 1)
    print(f"engine {s1['cpu']} %, slowest tick {s1['tick_max_us']} us of 725, longest gap {s1['gap_max_us']} us, "
          f"late {s1['late'] - s0['late']}, {(s1['ticks'] - s0['ticks']) / secs:.0f} ticks a second")
    print(f"screen {fr / secs:.1f} frames a second, {(s1['ui_us'] - s0['ui_us']) / fr / 1000:.2f} ms a frame, "
          f"slowest {s1['ui_us_max'] / 1000:.2f} ms")
    drops = s1["drops"] - s0["drops"]
    print(f"MIDI out {(s1['events'] - s0['events']) / secs:.0f} a second (dropped {drops & 0xFFFF} in the engine, "
          f"{drops >> 16} at USB; the USB ring's most {s1.get('usb_hw', 0)}), in {s1['midi_in'] - s0['midi_in']}; "
          f"tempo {s1['tempo'] / 10:.1f}, {'playing' if s1['flags'] & 1 else 'stopped'}"
          f"{', following a clock' if s1['flags'] & 2 else ''}{', recording' if s1['flags'] & 4 else ''}")


def play(dev):
    r = dev.ask(7)
    v = [r35(r, 5 * k) for k in range(6)]
    for s in range(4):
        print(f"sequencer {s + 1}: {MODES[(v[s] >> 16) & 15] if (v[s] >> 16) & 15 < 6 else '?'}, pattern "
              f"{(v[s] >> 8 & 255) + 1}, step {(v[s] & 255) + 1}{', muted' if v[s] >> 20 & 1 else ''}")
    print(f"bar {v[4] + 1}, beat {v[5] + 1}")


def store(dev):
    r = dev.ask(5)
    v = [r35(r, 5 * k) for k in range(8)]
    print(f"flash {'found' if v[0] else 'NOT FOUND'}; last saves: log {v[1] - 100}, settings {v[2] - 100}, "
          f"panel {v[3] - 100}; {v[6]} of {v[7]} bytes in use")
    for s in range(4):
        m = (v[4 + s // 2] >> (16 * (s & 1))) & 0xFFFF
        print(f"sequencer {s + 1}: patterns saved " + (" ".join(str(p + 1) for p in range(16) if m >> p & 1) or "none"))


def timing(dev, secs):
    if not dev.status().get("bench"):
        sys.exit("fm1_status: timing needs a bench build (./build.sh --bench)")
    dev.ask(6, [0], reply=False)
    dev.ask(2, [2], reply=False)
    time.sleep(0.5)
    dev.drain()
    t = []

    def cb(m):                                   # stamped as it arrives
        if m.type == "note_on" and m.velocity and m.channel == 0:
            t.append(time.perf_counter())
    dev.i.callback = cb
    dev.o.send(mido.Message("sysex", data=HDR + [6, 1]))
    time.sleep(secs)
    dev.i.callback = None
    dev.o.send(mido.Message("sysex", data=HDR + [6, 0]))
    if len(t) < 10:
        sys.exit(f"fm1_status: only {len(t)} notes came")
    iv = [(t[k + 1] - t[k]) * 1e3 for k in range(len(t) - 1)]
    mean = sum(iv) / len(iv)
    sd = (sum((x - mean) ** 2 for x in iv) / len(iv)) ** 0.5
    n = len(t)
    mx, my = (n - 1) / 2, sum(t) / n
    k = sum((x - mx) * (y - my) for x, y in enumerate(t)) / sum((x - mx) ** 2 for x in range(n))
    worst = max(abs(y - (my + k * (x - mx))) for x, y in enumerate(t)) * 1e3
    print(f"{n} notes: {mean:.3f} ms apart (125.000 expected), standard deviation {sd:.3f} ms, "
          f"from {min(iv):.3f} to {max(iv):.3f}; the worst {worst:.3f} ms off a straight line; "
          f"tempo {60.0 / (k * 4):.4f} BPM")


def main():
    a = sys.argv[1:] or ["load"]
    if a[0] not in ("load", "play", "store", "timing"):
        sys.exit(__doc__)
    dev = Dev()
    if a[0] == "load":
        load(dev, float(a[1]) if len(a) > 1 else 5.0)
    elif a[0] == "play":
        play(dev)
    elif a[0] == "store":
        store(dev)
    else:
        timing(dev, float(a[1]) if len(a) > 1 else 10.0)
    sys.stdout.flush()
    os._exit(0)                                  # (closing a port while a lot of MIDI comes in can hang)


main()
