#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""docs/img/seq4.gif: the README's animation, from the simulator. The demo playing, each of the four sequencers
in turn (a bass, chords, drums, a lead that follows the chords), then the patterns; a frame every 100 ms.

  tools/readme_gif.py        (builds the simulator first; needs Pillow)"""
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parents[1]
STEP_MS = 100
# what happens, and for how long: (commands before, frames)
PLAN = [(["tap PLAY"], 16)]                           # sequencer 1, the bass
for _ in range(3):                                     # 2 the chords, 3 the drums, 4 the lead
    PLAN.append((["turn ALGO 1"], 14))
PLAN.append((["turn ALGO -3", "press HOME", "wait 300"], 10))   # the patterns, held
PLAN.append((["release HOME"], 6))


def main():
    subprocess.run(["sh", str(SRC / "host" / "build_host.sh")], check=True, capture_output=True)
    with tempfile.TemporaryDirectory() as tmp:
        lines, n = ["wait 300"], 0
        for cmds, frames in PLAN:
            lines += cmds
            for _ in range(frames):
                lines += [f"wait {STEP_MS}", f"shot f{n:03d}.png"]
                n += 1
        script = Path(tmp) / "gif.s4"
        script.write_text("\n".join(lines) + "\n")
        subprocess.run([str(SRC / "build" / "host" / "seq4_host"), str(script), tmp], check=True, capture_output=True)
        frames = [Image.open(Path(tmp) / f"f{i:03d}.png").convert("RGB") for i in range(n)]
    out = [f.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    dest = SRC / "docs" / "img" / "seq4.gif"
    out[0].save(dest, save_all=True, append_images=out[1:], duration=STEP_MS, loop=0, optimize=True)
    print(f"{dest}: {n} frames, {dest.stat().st_size // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
