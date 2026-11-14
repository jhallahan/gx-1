#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""GX-1 parity: what the firmware plays (the host simulator's MIDI log) against what the genre engine rendered.

    python3 tests/gx_parity.py DIR [fill]   (DIR holds expected.txt from `gx dump` and midi_out.txt;
                                             fill: PLAY was held for the fill through the phrase)

Every note the engine rendered (ratchets expanded; live chances excepted) must start on the device on the
same channel and note, within the device's timing resolution (a tick is 0.73 ms; offsets are 1 % of a 16th),
with the same velocity; pitched notes must also end where the engine said (tied steps, last gate). No other
note may start, except the live chances, which the device rolls itself. The fills on demand: a note for the
fill (chance 101) plays only with the fill held, one the fill takes out (102) only without it.
"""
import pathlib
import sys

TOL_ON_MS = 3.0        # a tick (0.73 ms) + the offset's 1 % rounding (1.15 ms at 130 BPM) + margin
TOL_OFF_MS = 6.0       # the last step's gate is a whole % of a step; ties end on the grid
TOL_RETRIG_MS = 9.0    # a note cut for the same note again ends a 95 % gate before it
# a Mono part's note that runs into the next (another note on its channel starts before it ends) is legato:
# on the FM-1 it ends just after the next begins (SEQ-4's LEG_U), so it must end between that start and
# where the engine said: the overlap (which a synth's glide or legato hears) is kept, not its exact length


def main():
    d = pathlib.Path(sys.argv[1])
    exp = (d / "expected.txt").read_text().split("\n")
    bpm = float(exp[0].split()[1])
    ms_per_unit = 60000.0 / (3840.0 * bpm)
    events = [tuple(int(x) for x in line.split()) for line in exp[1:] if line.strip() and not line.startswith("#")]
    fill = len(sys.argv) > 2 and sys.argv[2] == "fill"
    events = [e[:7] + (0 if e[7] in (101, 102) else e[7],) for e in events if e[7] != (102 if fill else 101)]

    t0, ons, offs = None, [], []
    for line in (d / "midi_out.txt").read_text().splitlines():
        parts = line.split()
        t = float(parts[0])
        st, d1, d2 = (int(x, 16) for x in parts[3:6])
        if st == 0xFA and t0 is None:
            t0 = t
        if t0 is None:
            continue
        if st & 0xF0 == 0x90 and d2 > 0:
            ons.append([t - t0, (st & 15) + 1, d1, d2, False])
        elif st & 0xF0 == 0x80 or (st & 0xF0 == 0x90 and d2 == 0):
            offs.append((t - t0, (st & 15) + 1, d1))
    if t0 is None:
        sys.exit(f"{d}: no start in the log")
    phrase_ms = 8 * 4 * 3840 * ms_per_unit
    ons = [o for o in ons if o[0] < phrase_ms - 1.0]             # the first pass of the 8 bars

    missing, velbad, durbad, worst_on, worst_off, n_exp = [], [], [], 0.0, 0.0, 0
    live = []
    for (t, ln, role, ch, note, vel, rep, cond) in events:
        hits = [t + k * ln // rep for k in range(rep)] if rep > 1 else [t]
        for k, u in enumerate(hits):
            want = u * ms_per_unit
            if cond:
                live.append((want, ch, note))
                continue
            n_exp += 1
            best = None
            for o in ons:
                if not o[4] and o[1] == ch and o[2] == note and abs(o[0] - want) <= TOL_ON_MS:
                    if best is None or abs(o[0] - want) < abs(best[0] - want):
                        best = o
            if best is None:
                missing.append((round(want, 1), ch, note, role))
                continue
            best[4] = True
            worst_on = max(worst_on, abs(best[0] - want))
            if k == 0 and best[3] != vel:
                velbad.append((round(want, 1), ch, note, vel, best[3]))
            if role in (1, 2, 3) and rep == 1:                    # pitched: where the note ends
                end = (t + ln) * ms_per_unit
                nxt = [u2 * ms_per_unit for (u2, _l, _r, c2, n2, *_x) in events if c2 == ch and n2 == note and u2 > t]
                if nxt and min(nxt) < end:                        # the same note again: it retriggers
                    end = min(nxt) - 0.0
                    if abs(end - want) < 0.5:
                        continue
                off = next((f[0] for f in offs if f[1] == ch and f[2] == note and f[0] > best[0] + 0.01), None)
                cut = [u2 * ms_per_unit for (u2, _l, r2, c2, n2, *_x) in events
                       if c2 == ch and r2 == role and n2 != note and t < u2 and u2 * ms_per_unit < end - 0.01]
                if role in (1, 3) and cut and off is not None:    # Mono legato into the next note
                    if not (min(cut) - TOL_ON_MS <= off <= end + TOL_OFF_MS):
                        durbad.append((round(want, 1), ch, note, round(end, 1), round(off, 1), "legato"))
                    continue
                if off is not None and end < phrase_ms - 1.0:
                    err = abs(off - end)
                    worst_off = max(worst_off, err)
                    if err > (TOL_RETRIG_MS if nxt and min(nxt) <= end + 0.01 else TOL_OFF_MS):
                        durbad.append((round(want, 1), ch, note, round(end, 1), round(off, 1)))
    extra = []
    for o in ons:
        if o[4]:
            continue
        if any(abs(o[0] - w) <= TOL_ON_MS and o[1] == c and o[2] == n for (w, c, n) in live):
            continue                                              # a live chance the device rolled
        extra.append((round(o[0], 1), o[1], o[2]))
    ok = not (missing or velbad or durbad or extra)
    print(f"{d.name}: {n_exp} notes expected, {len(missing)} missing, {len(extra)} extra, {len(velbad)} velocity, "
          f"{len(durbad)} length; worst onset {worst_on:.2f} ms, worst end {worst_off:.2f} ms; "
          f"{len(live)} live chances -> {'OK' if ok else 'FAIL'}")
    for name, lst in (("missing", missing), ("extra", extra), ("velocity", velbad), ("length", durbad)):
        for x in lst[:6]:
            print(f"   {name}: {x}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
