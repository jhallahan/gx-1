/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TS-1: a pulse sequencer. 32 tracks (TS1_TRACKS), each a ring of steps with pulses spread evenly over it (Euclidean),
 * turned by a rotation, on its own clock; repeats (ratchets) inside a step, accents spread over the pulses, a
 * chance and a "temperature" that loosens velocity, timing and pitch. A melodic track walks a phrase through the
 * song's scale and can stack harmony. Integer-only, no allocation, no state between calls: the notes of any span
 * of time are a pure function of the song, so a host renders ahead, seeks and loops freely. The same C runs on
 * the FM-1 and, as WebAssembly, in the browser. */
#ifndef TS1_H
#define TS1_H
#include <stdint.h>

#define TS1_PPQ 96u                    /* ticks a quarter note */
#ifndef TS1_TRACKS
#define TS1_TRACKS 32u                 /* the web and Live: 32; a small box can build with 16 */
#endif
#define TS1_MAXSTEPS 64u

enum { TS1_DIV_32, TS1_DIV_16T, TS1_DIV_16, TS1_DIV_8T, TS1_DIV_8, TS1_DIV_4T, TS1_DIV_4, TS1_DIV_2, TS1_DIV_1, TS1_NDIV };
enum { TS1_PH_HOLD, TS1_PH_UP, TS1_PH_DOWN, TS1_PH_UPDOWN, TS1_PH_RANDOM, TS1_PH_WALK, TS1_PH_CHORD, TS1_NPHRASE };
enum { TS1_SC_MAJOR, TS1_SC_MINOR, TS1_SC_DORIAN, TS1_SC_PHRYGIAN, TS1_SC_LYDIAN, TS1_SC_MIXOLYDIAN, TS1_SC_HARMMINOR,
       TS1_SC_PENTMAJ, TS1_SC_PENTMIN, TS1_SC_BLUES, TS1_SC_CHROMATIC, TS1_NSCALE };

/* a track's parameters, by number (ts1_get / ts1_set; the web page and the FM-1's encoders use these) */
enum {
    TS1_P_ON,        /* 0 muted, 1 playing */
    TS1_P_KIND,      /* 0 drum (one note), 1 melodic (the scale) */
    TS1_P_CH,        /* MIDI channel 0..15 */
    TS1_P_NOTE,      /* drum: its note; melodic: the base note (C of its octave, 0..127) */
    TS1_P_STEPS,     /* 1..64 */
    TS1_P_PULSES,    /* 0..steps: how many hits, spread evenly */
    TS1_P_ROTATE,    /* 0..steps-1 */
    TS1_P_DIV,       /* TS1_DIV_* */
    TS1_P_SUSTAIN,   /* note length, 1..400 % of a step (over 100: held into the next steps) */
    TS1_P_REPEATS,   /* 1..8 hits in each step that plays */
    TS1_P_PACE,      /* repeats speeding up (-3..-1), even (0), slowing (1..3) */
    TS1_P_ACCENT,    /* 0..pulses: accented hits, spread over the pulses */
    TS1_P_VEL,       /* 1..127 */
    TS1_P_ACCVEL,    /* 0..64 added on an accent */
    TS1_P_GROOVE,    /* 50..75: every second step late (swing) */
    TS1_P_PROB,      /* 0..100 % chance a pulse plays */
    TS1_P_TEMP,      /* 0..100 temperature: looser velocity, timing and pitch */
    TS1_P_PHRASE,    /* TS1_PH_*: how a melodic track walks */
    TS1_P_RANGE,     /* 1..16 scale degrees it walks over */
    TS1_P_DEGREE,    /* -7..14 the degree it starts from */
    TS1_P_VOICES,    /* 1..4 notes stacked in thirds (harmony) */
    TS1_P_SPREAD,    /* 0 close, 1 open (the middle voice up an octave) */
    TS1_P_SEED,      /* 0..255: a different roll of the dice for chance and temperature */
    TS1_P_FOLLOW,    /* 0/1: a melodic track follows the song's chords (its degrees move with the chord's root) */
    TS1_P_MLEN,      /* 0..16 the melody's length in notes, apart from the rhythm's (0: as long as the phrase) */
    TS1_P_ACCP,      /* what an accent does to the pitch: 0 nothing, 1 an octave up, 2 a fifth up, 3 an octave down */
    TS1_NPARAM
};
/* the song: tempo in BPM x 10; root 0..11; the scale; the chord progression (0 none, 1..TS1_NPROG) and the bars
   each chord lasts (1, 2, 4 or 8); the master swing (50..75, as a track's groove: every track gets it, a track's
   own groove on top) */
enum { TS1_S_TEMPO, TS1_S_ROOT, TS1_S_SCALE, TS1_S_PROG, TS1_S_BPC, TS1_S_SWING, TS1_NSONG };
#define TS1_NPROG 8

typedef struct { int16_t v[TS1_NPARAM]; } ts1_track_t;
typedef struct {
    ts1_track_t trk[TS1_TRACKS];
    int16_t s[TS1_NSONG];
} ts1_song_t;

typedef struct {
    uint32_t t, len;                   /* ticks */
    uint8_t track, ch, note, vel;
} ts1_event_t;

extern const uint16_t ts1_div_ticks[TS1_NDIV];
extern const char *const ts1_param_name[TS1_NPARAM];
extern const char *const ts1_scale_name[TS1_NSCALE];
extern const char *const ts1_prog_name[TS1_NPROG + 1];          /* [0] "None", then the scale degrees: "1-6-3-7" */

void ts1_init(ts1_song_t *s);                                   /* sixteen tracks, a sensible start */
int ts1_get(const ts1_song_t *s, int track, int param);
void ts1_set(ts1_song_t *s, int track, int param, int value);   /* clamped to the parameter's range */
int ts1_lo(int param, const ts1_song_t *s, int track);          /* a parameter's range (steps-dependent ones too) */
int ts1_hi(int param, const ts1_song_t *s, int track);
int ts1_pulse(int steps, int pulses, int rotate, int step);     /* 1: the step plays (Euclidean) */
int ts1_scale_note(int scale, int degree);                      /* semitones above the root, any degree */
int ts1_chord_degree(const ts1_song_t *s, uint32_t tick);       /* the chord's root (a scale degree) at a tick: 0 with none */
int ts1_prog_len(int prog);                                      /* how many chords progression `prog` has: 1 for none */
/* the notes that start in [from, to), in time order; at most max written; returns how many there are */
uint32_t ts1_render(const ts1_song_t *s, uint32_t from, uint32_t to, ts1_event_t *out, uint32_t max);
#endif
