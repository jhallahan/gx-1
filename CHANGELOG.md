# Changelog

Each release's notes come from its section here (`tools/release_notes.py`). The newest is first.

## 0.1.0 — 2026-10-16

The first release.

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
