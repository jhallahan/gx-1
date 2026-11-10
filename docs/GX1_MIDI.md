# GX-1 and a MIDI controller

GX-1 listens for a controller on **MIDI channel 16**, on the MIDI jack and over USB. A controller on that
channel drives the GEN pages as the panel does: the macros, the tracks' mutes and locks, new ideas, mutate,
re-roll, undo and the six parts. The other fifteen channels are SEQ-4's, as before (recording, transposing,
its transport CCs and program changes).

The phone and desktop player will use the same map, so a controller set up once works with both.

## The map (channel 16)

### Knobs: control change (absolute, 0–127)

| CC | Does |
|---|---|
| 20 | Density |
| 21 | Sync |
| 22 | Space |
| 23 | Groove |
| 24 | Human |
| 25 | Tension (Colour) |
| 26 | Complex |
| 27 | Evolve |

A macro applies when the knob rests, the same as K1–K4.

### Buttons: control change (64 and up is on, or a press)

| CC | Does |
|---|---|
| 52–55 | Mute drums, sub, chords, upper (64 and up: muted, below: playing). Set these buttons to *toggle* |
| 56–59 | Lock drums, sub, chords, upper (64 and up: locked). *Toggle* |
| 60–63 | Select drums, sub, chords, upper |
| 85 | New idea (FX) |
| 86 | Mutate the selected track (SEL) |
| 87 | Re-roll the selected track (GLO + FX) |
| 88 | Undo (EDIT) |
| 89 | Redo (GLO + EDIT) |
| 90 | The next page: GEN, TRACK, ARRANGE (HOME) |

SEQ-4's transport CCs (105 play, 106 stop, 107 record) stay SEQ-4's even when its transport channel is 16.

### Pads: notes (a press)

| Note | Does |
|---|---|
| 36 (C1) | New idea |
| 37 | Mutate |
| 38 | Re-roll |
| 39 | Undo |
| 40–43 | Select drums, sub, chords, upper |
| 44–47 | Mute / unmute drums, sub, chords, upper |
| 48–53 | The six parts (they start at the next bar) |
| 54 | Redo |
| 55 | The next page |

Pads 36–51 cover a 16-pad controller's default layout: actions on the first row, track select on the second,
mutes on the third, and the first four parts on the fourth.

### Program change

Program change 0–5 on channel 16 picks the part (it starts at the next bar), so a DAW can play the arrangement.

## Which controller

Anything that sends notes and CCs on a channel you can set works. For the FM-1's MIDI jack it needs a MIDI
output on a jack (5-pin DIN or 3.5 mm TRS), not only USB; for a computer, USB is enough.

- **Arturia BeatStep (the original, not the Pro)**: the fit for GX-1. Sixteen endless knobs (the eight macros
  and room to spare), sixteen pads (the note map above is its default layout), USB and a 5-pin DIN MIDI out.
  Small, often second-hand. Set it up in Arturia's MIDI Control Center: the global channel 16, knobs 1–8 to
  CC 20–27 (absolute), pads to notes 36–51.
- **Korg nanoKONTROL2**: eight knobs, eight faders and buttons, USB only. Good with a computer. The knobs on
  CC 20–27; its S, M and R buttons on CC 60–63, 52–55 and 56–59 (toggle for M and R).
- **Novation Launch Control XL**: more knobs and faders than GX-1 needs, USB only. A good desk controller for
  the computer version.

The FM-1's jack is a 3.5 mm TRS MIDI input: a DIN controller needs a TRS-to-DIN adapter of the right type
(check which type your FM-1 takes; the wrong type gets no MIDI, but does no harm).

## Coming next

Learn (move a knob while holding a control on screen), the harmony (progression, chord length) on CCs, MIDI out
of the player to a computer, and a chord played on a keyboard taken as the key and the progression.
