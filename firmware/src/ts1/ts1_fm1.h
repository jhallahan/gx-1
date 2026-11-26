/* SPDX-License-Identifier: GPL-3.0-only */
/* Pulse (TS-1) on the FM-1: 16 tracks, played by the sequencer (seq/s4_pulse.c) while Pulse mode is on.
 * Include this, not ts1.h, so that every unit sees the same track count. */
#ifndef TS1_FM1_H
#define TS1_FM1_H
#define TS1_TRACKS 16u
#include "ts1.h"

extern ts1_song_t s4_ps;                 /* the Pulse song (the UI edits it; the sequencer plays it) */
extern volatile uint8_t s4_pulse_on;     /* 1: Pulse mode: the four sequencers rest, the song plays */
extern volatile uint32_t s4_pulse_hit[TS1_TRACKS];   /* the engine time each track last played (the rings flash) */
extern volatile uint32_t s4_pulse_pos;   /* the song's tick now (TS1_PPQ a quarter) */
#endif
