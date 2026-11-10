/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Genre profiles: data, not code (ADR-006). Authored in profiles/<genre>.toml, compiled by tools/profilec.py
 * into engine/src/gx_profiles_gen.c (const tables in flash on the device). */
#ifndef GX_PROFILE_H
#define GX_PROFILE_H

#include "gx.h"

#define GX_CYCLE_STEPS 32u     /* drum weights cover a 2-bar cycle */

typedef enum { GX_KIT_MACHINE = 0, GX_KIT_BREAK = 1 } gx_kit_t;

typedef struct {
    uint8_t w[GX_CYCLE_STEPS];   /* onset weight per step of the 2-bar cycle, 0..255 */
    uint32_t anchor;             /* bit k: step k of the cycle always plays (unless the lane is fill-muted) */
    uint8_t acc[16];             /* velocity level per step of a bar, 0..9 (0 = vel_lo, 9 = vel_hi) */
    uint8_t vel_lo, vel_hi;
    uint8_t len;                 /* note length, in 16ths (>= 1) */
    int8_t micro;                /* fixed timing offset, % of a 16th (-45..45): push (-) or drag (+) */
    uint8_t dens;                /* how strongly Density scales this lane, % (0..200) */
    uint8_t sync;                /* how strongly Syncopation moves weight off the beat, % (0..200) */
    uint8_t space;               /* inside a Space window: chance (0..255) a non-anchor hit is removed */
    uint8_t max;                 /* hits per bar at most (0 = no limit) */
    uint16_t exclude;            /* bit l: no hit on a step where lane l hits (kick B vs kick A, choke) */
    uint8_t ghost;               /* 1: hits are ghost notes (flagged) */
    uint8_t live;                /* 1: non-anchor steps are live chances (cond = the weight as a %), rolled
                                  * each time round on the FM-1 instead of decided once */
    uint8_t ab_a, ab_b;          /* A:B: the lane plays only in cycle A of every B 2-bar cycles (B 0 = always) */
    /* fills (the last fill_steps of a fill bar) */
    uint8_t fill_set;            /* 1: fill_w replaces the weights in the fill region */
    uint8_t fill_mute;           /* 1: the lane is silent in the fill region (anchors too): subtractive fills */
    uint8_t fill_w[16];          /* weights over the fill region, from its first step */
    uint8_t rep_p;               /* break edits: chance (0..255) a fill hit becomes a ratchet */
    uint8_t rep_max;             /* ratchet: up to this many hits in the step (2..5) */
} gx_lane_prof_t;

/* ---- harmony and pitched parts (M3) ---- */

typedef enum { GX_SCALE_AEOLIAN = 0, GX_SCALE_DORIAN, GX_SCALE_PHRYGIAN, GX_SCALE_IONIAN, GX_NSCALE } gx_scale_t;
typedef enum { GX_COL_TRIAD = 0, GX_COL_SEVENTH, GX_COL_ADD9, GX_COL_SUS2, GX_COL_ELEVENTH, GX_NCOL } gx_colour_t;
typedef enum { GX_UPPER_NONE = 0, GX_UPPER_ARP, GX_UPPER_MID, GX_UPPER_MOTIF } gx_upper_mode_t;
typedef enum { GX_ARP_UP = 0, GX_ARP_UPDOWN, GX_ARP_CYCLE5, GX_ARP_WALK } gx_arp_t;

#define GX_MAXPROG 4u
#define GX_PROGLEN 8u

typedef struct {
    uint8_t scale;               /* gx_scale_t */
    uint8_t key;                 /* default root pitch class, 0 = C */
    uint8_t nprog;               /* progressions to choose from (per seed) */
    uint8_t prog_len[GX_MAXPROG];
    uint8_t prog[GX_MAXPROG][GX_PROGLEN];   /* scale degrees 0..6 (i = 0 .. VII = 6) */
    uint8_t nbpc;                /* bars per chord: options to choose from (per seed) */
    uint8_t bpc[3];
    uint8_t colour[GX_NCOL];     /* weights of each chord colour, 0..9 */
} gx_harm_prof_t;

typedef struct {
    uint8_t stab_p;              /* chance (0..255) a seed plays rhythmic stabs instead of held pads */
    uint8_t w[16];               /* stab onset weights over a bar */
    uint8_t gate;                /* stab length, 16ths */
    uint8_t lo, hi;              /* voicing register (MIDI notes) */
    uint8_t voices;              /* 3..5 */
    uint8_t vel_lo, vel_hi;
    uint8_t antic;               /* pads: chance (0..255) a chord change is anticipated by a 16th or two */
} gx_chord_prof_t;

typedef struct {
    uint8_t w[GX_CYCLE_STEPS];   /* onset weights over the 2-bar cycle */
    uint32_t anchor;
    uint8_t lo, hi;              /* register */
    uint8_t at_kick;             /* weight x % on a step with a kick (>100 reinforce, <100 avoid) */
    uint8_t after_kick;          /* weight x % on the 1-2 steps after a kick (complement) */
    uint8_t at_snare;            /* weight x % on a snare step */
    int8_t dens_follow;          /* -100..100: weight change % per unit of nearby drum activity */
    uint8_t len_min, len_max;    /* note length, 16ths */
    uint8_t legato;              /* chance (0..255) a note holds until the next one */
    uint8_t glide;               /* chance (0..255) a note glides (flag) */
    uint8_t move[4];             /* note choice weights 0..9: root, fifth, octave, seventh */
    uint8_t max;                 /* notes per bar at most (0 = no limit) */
    uint8_t vel_lo, vel_hi;
} gx_sub_prof_t;

typedef struct {
    uint8_t mode;                /* gx_upper_mode_t */
    uint8_t w[GX_CYCLE_STEPS];   /* onset weights over the 2-bar cycle */
    uint8_t lo, hi;              /* register */
    uint8_t pattern;             /* arp: gx_arp_t */
    uint8_t len;                 /* note length, 16ths */
    uint8_t vel_lo, vel_hi;
    uint8_t acc[16];             /* velocity level per step, 0..9 */
    uint8_t tension;             /* chance (0..255) of a colour tone (arp: the 9th; motif: b2 / b6) */
    uint8_t at_sub;              /* mid: weight x % on a sub attack and the step after (low = answer around it) */
} gx_upper_prof_t;

/* a part slot (M5): how each role plays in it. Each role applies its own genre's slot (blends stay coherent). */
typedef enum { GX_PD_FULL = 0, GX_PD_THIN, GX_PD_NOKICK, GX_PD_PERC, GX_PD_OFF } gx_part_drums_t;
typedef enum { GX_PS_ON = 0, GX_PS_OFF, GX_PS_LONG } gx_part_sub_t;
typedef enum { GX_PC_ON = 0, GX_PC_PAD, GX_PC_OFF } gx_part_chords_t;
typedef enum { GX_CC_NONE = 0, GX_CC_DRIFT, GX_CC_SWEEP, GX_CC_OPEN, GX_CC_CLOSED } gx_cc_mode_t;

typedef struct {
    const char *name;            /* shown on the panel */
    uint8_t drums;               /* gx_part_drums_t */
    int8_t drum_density;         /* added to the drums' Density macro */
    uint8_t ramp;                /* rising: Density climbs by up to this much across the phrase */
    uint8_t build;               /* snare roll: 0 none, 1 8ths -> 16ths, 2 8ths -> 16ths -> 32nds */
    uint8_t build_bars;          /* the roll covers the phrase's last N bars (0: all of it) */
    uint8_t silent_last;         /* 1: the last bar drops out (drums and bass): tension before the next part */
    uint8_t sub;                 /* gx_part_sub_t */
    uint8_t chords;              /* gx_part_chords_t */
    uint8_t upper;               /* 1 on, 0 off */
    uint8_t vary;                /* 0..127: a built-in variation of drums, sub and upper (the "B" part) */
    uint8_t cc;                  /* gx_cc_mode_t */
    int8_t tension;              /* added to the Tension macro in this part (a build leans in, a drop resolves) */
    int8_t complex;              /* added to the Complex macro in this part */
} gx_part_prof_t;

typedef struct {
    const char *name;
    uint16_t tempo_x10, tempo_min_x10, tempo_max_x10;
    uint8_t kit;                 /* gx_kit_t */
    uint8_t swing;               /* % of an 8th pair given to the first 16th (50 = straight) */
    uint8_t swing_range;         /* Groove moves swing by up to this many % either way */
    uint8_t vjit;                /* Humanise at 64: velocity jitter, +/- */
    uint8_t tjit;                /* Humanise at 64: timing jitter, +/- % of a 16th */
    uint8_t var;                 /* Evolution at 64: chance (0..255) a decision is re-rolled per bar */
    uint8_t space_base;          /* Space at 64: chance (0..255) a 2-bar cycle bar gets a silence window */
    uint8_t fill_every;          /* bars (0 = no fills) */
    uint8_t fill_steps;          /* the fill region: the last N 16ths of the fill bar (1..16) */
    uint8_t break_edits;         /* 1: fills may ratchet (jungle-style edits) */
    gx_lane_prof_t lane[GX_NLANE];
    gx_harm_prof_t harm;
    gx_chord_prof_t chords;
    gx_sub_prof_t sub;
    gx_upper_prof_t upper;
    gx_part_prof_t part[GX_NPART];
    uint8_t cc_number;           /* the controller the CC lane drives (74: filter cutoff) */
    uint8_t cc_target;           /* the role whose channel it goes to (GX_ROLE_CHORDS or GX_ROLE_UPPER) */
} gx_genre_prof_t;

/* the profile of a genre, or NULL (the test pattern has none) */
const gx_genre_prof_t *gx_profile(gx_genre_t g);

/* generated (gx_profiles_gen.c) */
extern const gx_genre_prof_t *const gx_profiles[GX_NGENRE];

/* families: styles grouped for picking (profiles/families.toml) */
typedef struct {
    const char *name;            /* "DNB", "TECHNO" */
    uint8_t n;                   /* styles in it */
    uint8_t g[8];                /* their genre ids (gx_genre_t) */
} gx_family_t;
extern const gx_family_t gx_families[];
extern const uint32_t gx_nfamily;

/* the drum context the pitched parts read (built from the drum render, or from locked drums) */
typedef struct {
    uint8_t kick[GX_MAXBARS * 16u];     /* 0 none, else velocity */
    uint8_t snare[GX_MAXBARS * 16u];
    uint8_t act[GX_MAXBARS * 16u];      /* drum hits on this step */
} gx_ctx_t;

/* the harmony timeline: per bar, the chord's root degree and its pitch classes */
typedef struct {
    uint8_t deg[GX_MAXBARS];            /* chord root, scale degree 0..6 */
    uint8_t root[GX_MAXBARS];           /* chord root pitch class */
    uint16_t pcs[GX_MAXBARS];           /* bit p: pitch class p is a chord tone */
    uint8_t col[GX_MAXBARS];            /* gx_colour_t */
    uint8_t change[GX_MAXBARS];         /* 1: a new chord starts on this bar */
    uint8_t prog;                       /* the progression used (0-based) */
    uint8_t bpc;                        /* bars per chord used */
} gx_harmony_t;

void gx_ctx_build(const gx_render_t *drums, uint32_t bars, gx_ctx_t *ctx);
void gx_harmony_build(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, gx_harmony_t *h);
void gx_chords_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                      gx_render_t *o);
void gx_sub_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                   const gx_ctx_t *ctx, gx_render_t *o);
void gx_upper_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                     const gx_ctx_t *ctx, const gx_render_t *sofar, gx_render_t *o);

/* mutations: apply a recipe's mutations to one role's events [start, o->n) (gx_mutate.c) */
void gx_mutate_apply(const gx_recipe_t *r, gx_role_t role, const gx_comp_t *c, const gx_genre_prof_t *p,
                     const gx_harmony_t *h, gx_render_t *o, uint32_t start);

/* the drum generator (gx_drums.c) */
void gx_drums_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p,
                     const gx_outmap_t *map, gx_render_t *o);

#endif
