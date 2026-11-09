# Changelog

Each release's notes come from its section here (`tools/release_notes.py`). The newest is first.

## 0.2.0 — GX-1

The first GX-1 release: SEQ-4 with a generative, genre-aware composer on top.

- **The GEN page**, where GX-1 starts: four generated tracks (drums, sub, chords, upper) in their own colours,
  four macros drawn as pictures, the selected track's 16 steps, the key, tempo, part and bar, and the chord playing
  or the whole progression in Roman numerals.
- **Twenty styles in five families:** DNB (Deep DnB, Liquid, Rollers, Jungle, Halftime, Autonomic), Breaks & bass
  (Breaks, UK garage, Electro, Dubstep), Techno (Minimal, Dub techno, Acid, Detroit, Hard), House (Deep house, Lo-fi
  house) and Leftfield (IDM, Downtempo, Ambient). ALGORITHM picks the family, GLO + ALGORITHM the style.
- **Steering:** a new idea (FX), re-roll one track (GLO + FX), mutate (SEL, or by aspect with GLO + W1–W5), lock
  (ENV), undo and redo (EDIT), an idea by its number (PRESETS). New ideas start at the next bar from bar 1.
- **Six song parts** a style (GLO + W9–W14), shown as tiles while GLO is held, coming in at the next bar.
- **Eight macros:** Density, Sync, Space, Groove, and with GLO Human, Tension (harmonic colour) and Complex
  (rhythmic and melodic detail), Evolve. The parts lean on Tension and Complex: a build leans in, a drop resolves.
  Each macro has its own picture: grey guides, the track's colour for what the knob does, a handle on the value.
- **Key and tempo:** OCT− and OCT+ change key; SELECT the tempo, with each style's usual range shown.
- **MIDI out** on port A, channels to suit an Elektron Tonverk: kicks 1, kit 2, sub 5, chords 7, upper 8.
- **Experimental:** a USB host probe (hold SEQ at power-on) to test plugging MIDI devices into the FM-1.
- The firmware is now `gx1-X.Y.Z.fwsc`, and the site has the GX-1 manual beside SEQ-4's.

## 0.1.0 — 2026-10-16

The first SEQ-4 release (devrvega), which GX-1 is built on.

- **Four MIDI sequencers**, each on its own channel and USB port (four ports, A–D: 64 channels), up to 128
  steps in eight pages, sixteen patterns each, all running together from one clock (its own, or USB or TRS
  MIDI clock).
- **Six modes:** Mono, Poly (seven notes a step, pick and place), Chord (types in two banks, voicings, spread,
  strum, bass, its own arpeggiator), Multitrack (eight drum tracks, each its own note, channel, length and
  division), Stochastic and Matriceal.
- **Steps:** velocity, gate up to tie and legato, octave, glide, timing offset, repeats or skip, and
  conditions (chance, A:B, previous, first, fill, fixed, locked and their opposites); ties by holding two
  steps; any division from four bars to 1/64, six directions, loops, swing.
- **On the panel:** the screen is the grid (lit pads in each sequencer's colour); the white keys are the
  steps, the black keys the rows (Multitrack: its tracks), the page keys and MUTE; the knobs a step's values
  or every step's; menus on the knobs; a card for each button when held.
- **The keyboard:** notes, the chord keyboard, drum pads; pitch bend, mod wheel, a roller.
- **Recording** live (count-in, metronome click, quantize, overdub, loop / one shot / extend), step by step,
  and MIDI capture.
- **Generators:** random perform, a random generator (with humanize, shuffle, invert), Euclid, and a drum
  generator on Grids' map.
- **Modulation:** two LFOs a sequencer, mod lanes (a CC, bend, pressure or program a step), MIDI mappings with
  learn; a harmonizer that follows another sequencer's chords; 31 scales.
- **Live:** mutes and solos, fills, snapshots, tap tempo, nudge and time bend; songs of patterns (ten, four
  lanes of fifteen slots); program changes and transport CCs both ways.
- **Saving:** a log in the FM-1's flash for the project, 64 patterns and the songs; saves while playing, tidies
  up while stopped; undo and redo.
- **Measured on an FM-1:** the engine ticks every 0.73 ms; all four sequencers at their busiest take at most
  545 µs of a tick; sixteenths at 120 BPM arrive within 0.4 ms (standard deviation) of each other.
- **In the browser:** the same code, playing your synths through Web MIDI.
- **Fixed before release:** changing patterns on sequencers 2 and 4, or giving back a snapshot, crashed the FM-1
  (a word copy of a sequencer that was not word aligned); and after a crash in the audio interrupt, safe mode's
  USB never came up. The tests now also run every scenario under the address and undefined-behaviour
  sanitizers.
