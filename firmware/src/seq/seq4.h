/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: four sequencers for MIDI instruments. The data the UI edits (s4_seq_t, one a sequencer: its
 * settings and its steps, all of which a pattern holds), the project's settings (s4_glob_t) and the
 * engine's calls. The engine runs from one interrupt (s4_tick, every ~0.7 ms on the FM-1), where it also
 * takes the MIDI coming in and puts out what it plays; the UI changes the steps directly and asks
 * for everything else (play, stop, a pattern, a live note) through a queue (s4_cmd...). Host-testable:
 * no hardware here, MIDI goes out through s4_out (the app's). */
#pragma once
#include <stdint.h>

#define S4_NSEQ 4
#define S4_NSTEP 128           /* steps a sequence, in 8 pages of 16 */
#define S4_NPAGE 8
#define S4_NTRK 8              /* Multitrack's tracks */
#define S4_NMX 4               /* Matriceal's tracks */
#define S4_NLANE 10            /* ... and their lanes */
#define S4_NMOD 8              /* mod lanes: 4 (8 in Multitrack, one a track) */
#define S4_NLFO 2
#define S4_NPAT 16             /* patterns a sequencer */
#define S4_POLY 7              /* notes a step, Poly */
#define S4_NPN 896             /* Poly's notes in all */
#define S4_NMAP 16             /* MIDI mappings a sequencer */
#define S4_NPORT 4             /* the USB ports A .. D */
#define S4_NSONG 10            /* songs (arrangements) */
#define S4_NSLOT 15            /* their slots, a lane */
#define S4_NUCH 12             /* user chord slots */
#define S4_U 3840u             /* the engine's time: units a quarter note (every division divides it) */

enum { M_MONO, M_POLY, M_CHORD, M_MULTI, M_STOCH, M_MATRIX, M_N };
/* a step's gate: 1..100 % of the step, then tied to the next, then legato into it */
#define S4_TIE 101
#define S4_LEG 102
/* rtg: the step's repeats (low 3 bits: 0..4 = x1..x5), skipped, muted */
#define S4_RTG_N 0x07u
#define S4_SKIP 0x08u
#define S4_MUTED 0x80u
/* cond: 0..100 the step's probability %, then the logic conditions */
enum {
    S4_C_AB = 101,             /* A:B, A 1..B, B 2..8: 35 of them (101 .. 135) */
    S4_C_NAB = 136,            /* not A:B (136 .. 170) */
    S4_C_PRE = 171, S4_C_NPRE, S4_C_FIRST, S4_C_NFIRST, S4_C_FIXED, S4_C_FILL, S4_C_NFILL, S4_C_LOCKED,
    S4_C_MAX = S4_C_LOCKED
};
/* directions */
enum { D_FWD, D_BACK, D_PEND, D_PONG, D_RAND, D_DRUNK, D_N };
/* divisions: units a step (s4_div_units) */
enum { DV_4BAR, DV_2BAR, DV_1BAR, DV_2, DV_4, DV_6, DV_8, DV_12, DV_16, DV_24, DV_32, DV_48, DV_64, DV_N };
#define S4_HARMO 31            /* the scale that follows a Chord or Poly sequencer */
#define S4_NSCALE 31

typedef struct {               /* a step: Mono, Multitrack (deg is then the track's note), Stochastic's notes */
    int8_t deg;                /* the note: scale degree from the root (a row), any octave */
    uint8_t vel;               /* 0: no step; 1..127 */
    uint8_t gate;              /* 1..100, S4_TIE, S4_LEG */
    int8_t oct;                /* -4..4 */
    uint8_t cond;
    uint8_t rtg;
    int8_t ofs;                /* timing offset, % of the step, -45..45 */
    uint8_t glide;             /* 0..100 */
} s4_step_t;

typedef struct {               /* a Poly note (sorted by step) */
    uint8_t step;
    int8_t deg;
    uint8_t vel, gate;
    int8_t oct;
    uint8_t cond, rtg;
    int8_t ofs;
} s4_pnote_t;

typedef struct {               /* a Chord step */
    int8_t deg;                /* the chord's root, a scale degree */
    uint8_t vel, gate;
    int8_t oct;
    uint8_t cond, rtg;
    int8_t ofs;
    uint8_t type, voi, spread; /* the chord (s4_chord), its voicing, extra notes an octave up (0..7) */
    int8_t strum;              /* -100..100 */
    uint8_t glide;
} s4_cstep_t;

typedef struct {               /* a Multitrack track's settings */
    uint8_t ch, port, note;    /* its channel (0..15), port (0..3), base note (MIDI) */
    uint8_t init, end, div, dir, rdiv;
    uint8_t mute, solo, rnd_on, lfo_on;
    uint8_t glide;
    uint8_t eu_len, eu_puls;
    int8_t eu_rot;
    uint8_t eu_inv;
    uint8_t dg_on, dg_dens;    /* the drum generator (tracks 1-3: kick, snare, hats), its density */
    int8_t rp_vel, rp_gate;    /* random perform, this track's */
    uint8_t rp_trig;
    int8_t rp_rtg;
    uint8_t pc, bank;          /* program change on load (0 none, else 1..128) */
    uint8_t loop_on, loop_a, loop_b, rsv;
} s4_trk_t;

typedef struct {               /* a Matriceal lane */
    uint8_t init, end, div, dir, rand, min, max, rsv;
} s4_lane_t;
enum { L_TRIG, L_NOTE, L_INTV, L_PULS, L_VEL, L_OCT, L_GATE, L_PROB, L_RTG, L_GLIDE };
typedef struct {               /* a Matriceal track: its lanes of 16 values (and some a chance each) */
    uint8_t ch, port;
    int8_t note;               /* the track's note offset (degrees) */
    uint8_t rlen;              /* reset length (triggers), 0 none */
    s4_lane_t lane[S4_NLANE];
    int8_t v[S4_NLANE][16];
    uint8_t p[S4_NLANE][16];
} s4_mxtrk_t;

typedef struct {               /* an arpeggiator */
    uint8_t mode;              /* 0 off, 1.. S4_ARP_* */
    int8_t oct;                /* octaves -3..3 */
    uint8_t gate, div;
    int8_t accent;             /* groove -100..100 */
    uint8_t len, puls, rpt;    /* the Euclidean rhythm: length, pulses, notes a pulse */
    int8_t rot;
    uint8_t hold, rsv[2];
} s4_arp_t;
enum { A_UP = 1, A_DOWN, A_UPDN, A_2U2D, A_ALTIN, A_ALTOUT, A_ALTIO, A_PINKY, A_THUMB, A_RAND, A_RAND1, A_ORDER, A_N };

typedef struct {               /* an LFO */
    uint8_t wave, rate;        /* rate: S4_LFO_SYNC synced values (16 bars .. 1/32), then free */
    uint8_t phase, amt;        /* amount 0..100 */
    int8_t ofs;
    uint8_t idest;             /* internal destination (S4_D_*) */
    uint8_t xtype, xnum;       /* MIDI: off, CC n, pitch bend, aftertouch, program */
    uint8_t xamt, retrig, rsv[2];
} s4_lfo_t;
enum { W_SINE, W_TRI, W_SAWUP, W_SAWDN, W_SQUARE, W_PULSE, W_SH, W_SMOOTH, W_EXPUP, W_EXPDN, W_STAIRS, W_N };
enum { X_OFF, X_CC, X_PB, X_AT, X_PC, X_N };

typedef struct {               /* a mod lane's settings */
    uint8_t on, xtype, xnum;   /* what it sends: X_*, the CC */
    uint8_t mx, mn;            /* its range 0..127 */
    uint8_t idest[2];
    int8_t iamt[2], iofs[2];
    uint8_t div, smooth, linked, init, end;
    int8_t offset;
    char name[4];
} s4_modl_t;

typedef struct {               /* a MIDI mapping: something coming in, what it does */
    uint8_t type;              /* 0 off, 1 CC, 2 note on, 3 note off */
    uint8_t ch;                /* 0 the sequencer's, 1..16, 17 any */
    uint8_t num, trk;          /* the CC or note; the tracks it acts on (a mask, 0 = the selected) */
    uint8_t fwd;               /* forwarded as CC fwd - 1 (0 none) */
    uint8_t idest[2];
    int8_t iamt[2], iofs[2];
    uint8_t para[2];           /* a setting it changes (S4_P_*) */
    int8_t pamt[2], pofs[2];
    uint8_t consume, rsv;
} s4_map_t;

typedef struct {               /* Stochastic */
    uint8_t prob[12];          /* each semitone's chance, 0..7 */
    uint8_t oct_up, oct_dn;    /* 0..3 */
    uint8_t div_long, div_short;   /* 0..3 */
    uint8_t pitch, rhythm;     /* how often a new note, a new rhythm: 0..7 */
    uint8_t next;              /* the chance of the next step playing, 0..7 */
    uint8_t rsv[3];
    s4_step_t buf[S4_NSTEP];   /* what it played (replayed while the chances are locked) */
} s4_stoch_t;

/* a sequencer: everything a pattern holds */
typedef struct {
    uint8_t mode, ch, port;    /* mode M_*, channel 0..15, port 0..3 */
    uint8_t pc, bank;          /* program change, bank select on load (0 none, else 1..128) */
    uint8_t punch;             /* launch quantization: 0 global, 1 none, 2.. bars 1,2,4,8 */
    uint8_t reset;             /* realign after so many bars (0 off) */
    uint8_t tpose;             /* transpose from notes on: 0 off, 1 its own channel, 2..17 channel 1..16 */
    uint8_t color;
    uint8_t scale, root, oct;  /* scale 0..30 (S4_HARMO), root 0..11, base octave 0..8 (C2 = 2: note 36) */
    uint8_t leader;            /* the sequencer a S4_HARMO scale follows */
    uint8_t swing;             /* 50 none .. 75 */
    uint8_t div, dir, rdiv;    /* division, direction, random division % */
    uint8_t init, end;         /* 0..127 */
    uint8_t loop_on, loop_a, loop_b, loop_rate;
    int8_t bend;               /* time bend, % */
    int8_t nudge;              /* time offset, ms */
    uint8_t vdef, gdef;        /* new steps' velocity and gate */
    int8_t rp_vel, rp_oct;     /* random perform: velocity, octave */
    uint8_t rp_trig;           /* trigger chance (100 none) */
    int8_t rp_rtg;             /* retriggers (+), skips (-) */
    int8_t rg_bias, rg_range;  /* random generator: centre (degrees), range (+ above only, - both ways) */
    uint8_t rg_rand, rg_dens;  /* variety, density (101.. the special ones: humanize, order, pitch, invert) */
    uint8_t rg_scale, rg_human, rg_ties;
    uint8_t ch_bank, ch_auto, ch_bass;   /* chords: bank A/B, auto voicing, bass */
    uint8_t poly_max;          /* Poly: notes a step at most */
    int8_t cg_spread, cg_type, cg_voi;   /* chords: global offsets */
    uint8_t glide_cc;          /* glides sent as portamento (CC 65 / 5) */
    uint8_t dg_x, dg_y, dg_chaos;        /* the drum generator */
    uint8_t mute;              /* muted (still running) */
    uint8_t follow;            /* the grid follows the playhead */
    uint8_t rsv[6];
    char name[8];
    s4_arp_t arp;              /* Poly's and Chord's own arpeggiator (on the sequence) */
    s4_arp_t karp;             /* the keyboard's */
    s4_lfo_t lfo[S4_NLFO];
    s4_modl_t ml[S4_NMOD];
    s4_map_t map[S4_NMAP];
    uint8_t uch_n[S4_NUCH];    /* user chords: notes in each (0 empty) */
    int8_t uch[S4_NUCH][8];    /* ... as semitones from their root */
    uint8_t uch_root[S4_NUCH];
    uint8_t mod[S4_NMOD][S4_NSTEP];      /* the mod lanes' values */
    union {
        s4_step_t mono[S4_NSTEP];
        struct {
            uint16_t n, rsv;
            uint8_t glide[S4_NSTEP];
            s4_pnote_t note[S4_NPN];
        } poly;
        s4_cstep_t chord[S4_NSTEP];
        struct {
            s4_trk_t t[S4_NTRK];
            s4_step_t s[S4_NTRK][S4_NSTEP];
        } multi;
        s4_stoch_t stoch;
        s4_mxtrk_t mx[S4_NMX];
        uint32_t align4;       /* (a sequencer 4-byte aligned, its size a multiple of 4: it is copied by words) */
    } d;
} s4_seq_t;

/* the project: what is not a pattern's */
typedef struct {
    uint32_t magic;
    uint16_t format, tempo;    /* tempo: BPM x 10, 300 .. 3000 */
    uint8_t clock;             /* clock source: 0 internal, 1 USB, 2 TRS, 3 any (auto) */
    uint8_t clk_out;           /* ports that get MIDI clock and start/stop (a mask) */
    uint8_t loadq;             /* pattern load quantization: 0 now, 1 step? 2.. 1,2,4,8 bars */
    uint8_t thru;              /* the TRS jack's messages, sent on: 0 off, 1..4 port A..D */
    uint8_t usb_thru;          /* USB in echoed back out (the same port) */
    uint8_t filt;              /* MIDI in channel filtering: 0 off, 1 channel, 2 channel and note */
    uint8_t pc_ch;             /* program changes load patterns: 0 off, 1..16 */
    uint8_t tcc_ch;            /* transport as CC 105-107: 0 off, 1..16 */
    uint8_t bank_lsb;          /* bank select as CC 32 (else 0) */
    uint8_t kq;                /* keyboard notes quantized to the scale */
    uint8_t mute_cc;           /* muted Multitrack tracks still send their mod lanes */
    uint8_t smooth;            /* mod lanes' default smoothing */
    uint8_t play_mode;         /* PLAY while playing: 0 stop, 1 pause, 2 reset */
    uint8_t mute_mode;         /* 0 toggle now, 1 toggle on release, 2 mute now, 3 mute on release */
    uint8_t keep_mutes, taps, karp_vel, latch, arp_reset, loop_sync, harm_jump, harm_base, load_bpm;
    uint8_t lock_scale, confirm_scale, lock_preview, trk_prio, reset_params, vdef, gdef;
    uint8_t met_mode;          /* metronome: 0 off, 1 count-in, 2 always */
    uint8_t met_vol;           /* the click's level 0..10 */
    uint8_t met_ch;            /* ... and as MIDI: 0 off, 1..16 (port A, note 37 / 36) */
    uint8_t count_in;          /* 0 off, 1 beat, 2 half bar, 3 bar, 4 two bars */
    uint8_t rec_q, rec_qpct, rec_over, rec_mode;   /* recording: quantize, its %, overdub, loop/1-shot/extend */
    uint8_t auto_sync, start_ign;
    uint8_t leds, glow;
    uint8_t rsv[16];
} s4_glob_t;

/* a song: four lanes of slots, each a pattern repeated so many times */
typedef struct {
    uint8_t pat;               /* 0..15, S4_BLANK a bar of silence, S4_EMPTY nothing */
    uint8_t rep;               /* 1..19, 0 for ever */
    uint8_t pc, bank;          /* 0 the pattern's, 255 none, else 1..128 */
} s4_slot_t;
#define S4_BLANK 16
#define S4_EMPTY 255
typedef struct {
    s4_slot_t slot[S4_NSEQ][S4_NSLOT];
    uint8_t master;            /* 0 none, 1..4 the lane the others follow */
    uint8_t launchq;           /* bars a repetition: 0 the pattern's own length, 1,2,4,8 */
    uint8_t rsv[2];
} s4_song_t;

/* ------------------------------------------------------------------ the engine's state (read-only) --- */
typedef struct {
    uint8_t on;                /* running */
    uint8_t ext;               /* following external clock */
    uint8_t rec;               /* recording */
    uint8_t fill;              /* the FILL modifier held */
    uint32_t pos;              /* the song position, units */
    uint32_t bar, beat;        /* bars and beats since start */
    uint16_t tempo;            /* BPM x 10 now (the external's, measured) */
    uint8_t step[S4_NSEQ][S4_NTRK];   /* each sequencer's (track's) step playing */
    uint8_t lit[S4_NSEQ][S4_NTRK];    /* a note played lately (for the screen: counts) */
    uint8_t pat[S4_NSEQ];      /* the pattern each plays */
    uint8_t next[S4_NSEQ];     /* the one waiting to load (0xFF none) */
    uint8_t want[S4_NSEQ];     /* the arranger wants this one (the UI stages it; 0xFF none) */
    uint8_t active[S4_NSEQ];   /* running (not stopped by its own mute) */
    uint8_t gen[S4_NSEQ][S4_NTRK][4];  /* generated triggers this bar (drum generator, Euclid mod) */
    uint8_t chord[S4_NSEQ][8]; /* the notes a Chord / Poly sequencer sounds now (the harmonizer's) */
    uint8_t chord_n[S4_NSEQ];
    uint32_t events;           /* MIDI messages out */
    uint32_t drops;            /* ... that found the queue full */
    uint32_t in;               /* MIDI messages in */
    uint8_t last_in[3];        /* the last one (the monitor) */
    uint8_t last_in_src;
    uint8_t song_on, song;     /* the arranger */
    uint8_t slot[S4_NSEQ];
    uint8_t reps[S4_NSEQ];
    uint8_t snap;              /* a snapshot taken */
    uint8_t count;             /* count-in: beats left */
    uint8_t perf_rpt;          /* GX-1's perform (FX held): the beat repeat's length in steps (0 off) */
    int8_t perf_oct;           /* ... the pitched sequencers an octave or two up or down */
    uint8_t perf_mute;         /* ... the sequencers it holds silent (a bit each) */
} s4_play_t;

extern s4_seq_t s4[S4_NSEQ];
extern s4_glob_t s4g;
extern s4_song_t s4song[S4_NSONG];
extern volatile s4_play_t s4p;

/* ------------------------------------------------------------------------------- the calls --- */
void s4_init(void);
void s4_defaults_glob(void);
void s4_defaults_seq(int s, int mode);     /* an empty sequencer in this mode (its settings kept) */
void s4_tick(uint32_t us);                 /* the interrupt: us since the last */
void s4_midi_in(uint32_t pkt);             /* a USB-MIDI packet: cable 0..3, 14 the TRS jack */
void s4_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2);   /* the app's: one message out */
void s4_click(int accent);                 /* the app's: the metronome's click */

/* the UI's requests (queued, done in the next tick) */
void s4_play(int on);
void s4_stop_reset(void);
void s4_fill(int on);
/* GX-1's perform (FX held), for as long as it is held: a beat repeat (the last `rpt` steps of each sequencer
 * again and again, 0 off: back where the song would be on release), the pitched sequencers `oct` octaves
 * up or down (the drum map's stay), the sequencers in `mute` silent. All at once; (0, 0, 0) lets go. */
void s4_perf(int rpt, int oct, int mute);
void s4_rec(int on);
void s4_live(int s, int trk, int note, int vel);   /* a key: note on (vel > 0) / off, sequencer s */
void s4_live_cc(int s, int trk, uint32_t st, uint32_t d1, uint32_t d2);
void s4_panic(void);
void s4_load_now(int s);                   /* s4[s] was replaced (a pattern loaded): restart its state */
void s4_queue_pattern(int s, int pat);     /* the staged pattern goes in at the next boundary */
void s4_queue_pattern_bar(int s, int pat); /* GX-1: ... at the next bar line, from its start */
void s4_mute(int s, int on);
void s4_tap(void);
void s4_restart(int s);                    /* -1 all */
void s4_snapshot(int take);                /* 1 take, 0 release (at the next bar), -1 cancel */
void s4_song(int on);
void s4_capture(int s);                    /* MIDI capture into s */
uint32_t s4_cap_total(void);              /* the notes played (not recording) since boot */
int s4_cap_get(uint32_t i, uint8_t *note, uint8_t *vel, uint8_t *trk);   /* the i-th: 0 if gone */

void s4_select(int s);                     /* the sequencer on screen (MIDI in goes to it unfiltered) */
void s4_roll(int s, int dv);               /* the roller: the keys held repeat at this division (0 off) */
void s4_arp_clear(int s);                  /* the keyboard arp's held notes let go */
void s4_rec_track(int t);                  /* Multitrack: the track recording goes to */
void s4_lock(int on);                      /* the app's: the engine's interrupt held off (1) / let go (0) */
extern s4_cstep_t s4_kchord;               /* the chord the chord keyboard plays (recorded as it is) */
extern volatile uint8_t s4_pc_req;         /* a program change came for pattern n - 1 (the UI loads it) */
/* GX-1's MIDI control: on channel s4_gx_ch (1-16, 0 off) notes, CCs and program changes go to this queue for the
   GEN pages (docs/GX1_MIDI.md), not to the sequencers. Written in the audio interrupt, read in the main loop. */
#define S4_GXQ 32u
extern volatile uint32_t s4_gxq[S4_GXQ], s4_gxq_r, s4_gxq_w;
extern uint8_t s4_gx_ch;
extern volatile uint32_t s4_cap_count;     /* notes waiting to be captured */

/* GX-1: a Poly sequencer with rsv[0] & S4_RSV_DRUMMAP plays its degrees 0..15 as drum sounds, each sent as
 * s4_dm_note[deg] on channel s4_dm_ch[deg] (the drum map: ADR-024 in the engine repository) */
#define S4_RSV_DRUMMAP 0x01u
extern uint8_t s4_dm_note[16], s4_dm_ch[16];

/* the staging buffers (filled by the app, loaded by the engine at the boundary) */
extern s4_seq_t s4_stage[S4_NSEQ];
extern s4_seq_t s4_snapbuf[S4_NSEQ];

/* helpers for the UI */
int s4_note_of(int s, int deg, int oct);   /* a degree, an octave offset: its MIDI note (scale, root, octave) */
int s4_deg_of(int s, int note);            /* the nearest degree to a MIDI note */
int s4_scale_len(int sc);
const char *s4_scale_name(int sc);
const char *s4_note_name(int note, char *b);   /* "C#3" */
uint32_t s4_div_units(int dv);
const char *s4_div_name(int dv);
int s4_chord_notes(int s, const s4_cstep_t *c, uint8_t *out);   /* the notes a chord step sounds */
const char *s4_chord_name(int s, const s4_cstep_t *c, char *b);
int s4_chord_types(int s, int deg);        /* how many chord types the degree has (in the bank) */
const char *s4_detect_chord(const uint8_t *notes, int n, char *b);
int s4_seq_len(int s, int trk);            /* steps in its range */
uint32_t s4_rand(void);

/* editing (UI side): all of these work on s4[s] and can be undone */
void s4_undo_push(int s);
int s4_undo(void);
int s4_redo(void);
void s4_clear(int s, int trk, int full);
void s4_euclid(int s, int trk);
void s4_generate(int s, int trk);          /* the random generator (trk -1: all Multitrack tracks) */
void s4_rotate(int s, int trk, int by);
void s4_transpose(int s, int trk, int by);
void s4_expand(int s, int trk);
void s4_condense(int s, int trk);
void s4_duplicate(int s, int trk);
void s4_set_scale(int s, int sc, int quantize);
void s4_set_mode(int s, int mode);
int s4_poly_find(int s, int step, int deg);
int s4_poly_add(int s, const s4_pnote_t *n);
void s4_poly_del(int s, int i);
void s4_stoch_lock(int s);

/* patterns as bytes (storage, undo): length written, 0 if it did not fit */
extern uint8_t s4_packbuf[12288];          /* (main loop only: undo, and the project's saves and loads) */
uint32_t s4_pack(const s4_seq_t *q, uint8_t *b, uint32_t max);
int s4_unpack(s4_seq_t *q, const uint8_t *b, uint32_t n);

/* internal destinations (LFOs, mod lanes, mappings) */
enum {
    S4_D_OFF, S4_D_VEL, S4_D_PITCH, S4_D_OCT, S4_D_GATE, S4_D_DIV, S4_D_RTG, S4_D_PROB, S4_D_GLIDE,
    S4_D_KARP_DIV, S4_D_KARP_OCT, S4_D_KARP_TYPE, S4_D_LFO1_WAVE, S4_D_LFO1_RATE, S4_D_LFO1_AMT,
    S4_D_LFO2_WAVE, S4_D_LFO2_RATE, S4_D_LFO2_AMT,
    S4_D_ARP_DIV, S4_D_ARP_OCT, S4_D_ARP_TYPE,             /* Poly, Chord */
    S4_D_CH_ROOT, S4_D_CH_TYPE, S4_D_CH_VOI, S4_D_STRUM, S4_D_SPREAD,   /* Chord */
    S4_D_EU_PULS, S4_D_EU_ROT, S4_D_DG_K, S4_D_DG_S, S4_D_DG_H, S4_D_DG_X, S4_D_DG_Y, S4_D_DG_CHAOS,   /* Multitrack */
    S4_D_ST_PROB, S4_D_ST_PITCH, S4_D_ST_RHYTHM, S4_D_ST_LONG, S4_D_ST_SHORT, S4_D_ST_UP, S4_D_ST_DN,   /* Stochastic */
    S4_D_MX_INTV,              /* Matriceal */
    S4_D_N
};
const char *s4_dest_name(int d);
int s4_dest_ok(int mode, int d);
/* settings a mapping changes (for good) */
enum {
    S4_P_OFF, S4_P_ENABLE, S4_P_MUTE, S4_P_DIV, S4_P_DIR, S4_P_SCALE, S4_P_ROOT, S4_P_SWING, S4_P_LOOP,
    S4_P_LOOP_OFF, S4_P_BEND, S4_P_NUDGE, S4_P_INIT, S4_P_END, S4_P_ML_ON, S4_P_ML_OFS, S4_P_RP_VEL, S4_P_RP_OCT,
    S4_P_RP_TRIG, S4_P_RP_RTG, S4_P_TEMPO, S4_P_FILL, S4_P_RESET, S4_P_GENERATE, S4_P_RELOAD, S4_P_TAP,
    S4_P_REC, S4_P_N
};
const char *s4_para_name(int p);
