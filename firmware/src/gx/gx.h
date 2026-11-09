/* SPDX-License-Identifier: GPL-3.0-or-later */
/* gx: the genre-aware composition engine.
 *
 * Rules (docs/02-architecture.md §2):
 *  - pure C99, no hardware, no dynamic allocation: every buffer is owned by the caller;
 *  - integer-only arithmetic on every path that produces output, so a seed renders
 *    bit-identically on x86, ARM, WASM and the FM-1 (pi32v2);
 *  - randomness is a hash of (seed, role, bar, step, purpose), never a shared stream,
 *    so regenerating one role never disturbs another.
 *
 * Time is in engine units: GX_PPQ units a quarter note, the same timebase as SEQ-4
 * (S4_U = 3840), so a sixteenth is 960 units. */
#ifndef GX_H
#define GX_H

#include <stdint.h>

#define GX_VERSION 1u          /* the generator version stored in recipes */

#define GX_PPQ 3840u           /* engine units a quarter note (SEQ-4's S4_U) */
#define GX_STEP (GX_PPQ / 4u)  /* a sixteenth */
#define GX_BAR_STEPS 16u       /* 4/4 only for now */
#define GX_BAR (GX_STEP * GX_BAR_STEPS)
#define GX_MAXBARS 32u

/* roles: the musical parts (ADR-012, ADR-018). Drums are one role with 10 lanes. */
typedef enum {
    GX_ROLE_DRUMS = 0,
    GX_ROLE_SUB,
    GX_ROLE_CHORDS,
    GX_ROLE_UPPER,             /* arp (Breaks), mid bass (DnB) or motif (Autonomic) */
    GX_NROLE
} gx_role_t;

/* the drum role's lanes (ADR-018): two kicks, then the kit */
typedef enum {
    GX_LANE_KICK_A = 0,
    GX_LANE_KICK_B,
    GX_LANE_SNARE,
    GX_LANE_GHOST,
    GX_LANE_CLAP,
    GX_LANE_CHAT,
    GX_LANE_OHAT,
    GX_LANE_RIDE,
    GX_LANE_PERC1,
    GX_LANE_PERC2,
    GX_NLANE
} gx_lane_t;

/* genres. GX_GENRE_TEST is a fixed, deterministic pipeline test pattern: not music.
 * The others have profiles (profiles/<genre>.toml); keep this order in step with tools/profilec.py GENRES. */
typedef enum {
    GX_GENRE_TEST = 0,
    GX_GENRE_DEEP_DNB,
    GX_GENRE_BREAKS,
    GX_GENRE_AUTONOMIC,
    GX_GENRE_DUB_TECHNO,
    GX_GENRE_IDM,
    GX_GENRE_ELECTRO,
    GX_GENRE_UK_GARAGE,
    GX_GENRE_MINIMAL,
    GX_GENRE_DOWNTEMPO,
    GX_GENRE_JUNGLE,
    GX_GENRE_HALFTIME,
    GX_GENRE_DEEP_HOUSE,
    GX_GENRE_ACID,
    GX_GENRE_DUBSTEP,
    GX_GENRE_LIQUID,
    GX_GENRE_ROLLERS,
    GX_GENRE_DETROIT,
    GX_GENRE_HARD_TECHNO,
    GX_GENRE_LOFI_HOUSE,
    GX_GENRE_AMBIENT,
    GX_NGENRE
} gx_genre_t;

/* macros (ADR-008): 0..127 each, 64 = the genre's default */
typedef enum {
    GX_M_DENSITY = 0, GX_M_SYNC, GX_M_SPACE, GX_M_GROOVE,
    GX_M_HUMAN, GX_M_TENSION, GX_M_COMPLEX, GX_M_EVOLVE,
    GX_NMACRO
} gx_macro_t;

/* mutations (M4): up to GX_MAXMUT per role, kept in the recipe so the result stays reproducible */
#define GX_MAXMUT 4u
#define GX_MUT_RHYTHM 0x01u    /* move hits a 16th; at high intensity, drop or add some */
#define GX_MUT_PITCH 0x02u     /* move notes to a neighbouring chord tone (pitched parts) */
#define GX_MUT_VEL 0x04u       /* re-shape velocities */
#define GX_MUT_LEN 0x08u       /* re-shape note lengths */
#define GX_MUT_ALL 0x0fu

/* a recipe: everything needed to regenerate one role bit-exactly (ADR-005) */
typedef struct {
    uint32_t seed;
    uint16_t engine_ver;
    uint8_t genre;             /* gx_genre_t, per role: this is genre blending */
    uint8_t macro[GX_NMACRO];
    uint8_t mut_n;             /* mutations applied on top, in order (0..GX_MAXMUT) */
    uint8_t mut_mask[GX_MAXMUT];   /* each mutation's aspects (GX_MUT_*) */
    uint8_t mut_int[GX_MAXMUT];    /* each mutation's intensity, 1..127 */
    uint32_t mut_seed[GX_MAXMUT];
} gx_recipe_t;

/* a composition: the recipes of every role plus the shared musical frame */
typedef struct {
    gx_recipe_t r[GX_NROLE];
    uint16_t bpm_x10;          /* tempo, BPM x 10 (e.g. 1720) */
    uint8_t key;               /* root pitch class, 0 = C */
    uint8_t bars;              /* phrase length to render: 1..GX_MAXBARS */
    uint8_t lock;              /* bit per role: locked roles keep their seed and their notes */
    uint8_t part;              /* the part slot playing (M5): 0 opening, 1 main (the default), 2 varied,
                                * 3 breakdown, 4 rising, 5 peak; each genre names and shapes its own */
    uint8_t prog;              /* the progression: 0 the idea picks one of the style's, 1..nprog that one */
    uint8_t bpc;               /* bars per chord: 0 the idea picks (from the style's), else 1, 2, 4 or 8 */
    gx_recipe_t lsub[2];       /* the sub's context when it was locked: drums, chords */
    gx_recipe_t lup[4];        /* the upper part's context when locked: the sub's drums and chords,
                                * the upper part's chords, the sub */
} gx_comp_t;

/* a rendered note */
#define GX_EF_GHOST 0x01u
#define GX_EF_ACCENT 0x02u
#define GX_EF_GLIDE 0x04u
#define GX_EF_ANCHOR 0x08u
/* a controller event (M5): role GX_ROLE_CC, lane = the role whose channel it goes to,
 * note = the controller number, vel = the value (0..127) */
#define GX_ROLE_CC 4u     /* a structural hit (backbeat, downbeat): mutation leaves it below intensity 64 */
typedef struct {
    uint32_t t;                /* start, engine units from the phrase start */
    uint32_t len;              /* duration, engine units (> 0) */
    uint8_t role;              /* gx_role_t */
    uint8_t lane;              /* drum lane (drums), else 0 */
    uint8_t note;              /* MIDI note 0..127 (raw numbers only, ADR-019) */
    uint8_t vel;               /* 1..127 */
    uint8_t flags;             /* GX_EF_* */
    uint8_t rep;               /* ratchet: 1 = a single hit, 2..5 = that many hits spread over len */
    uint8_t cond;              /* live chance: 0 = always plays; 1..100 = plays with that % chance each
                                * time it comes round (the FM-1 rolls it live: SEQ-4's step chance) */
} gx_event_t;

/* a render buffer, owned by the caller */
typedef struct {
    gx_event_t *ev;
    uint32_t n, cap;
    uint32_t dropped;          /* events that did not fit (should stay 0) */
} gx_render_t;

/* output map: where each role and drum lane goes (ADR-016, ADR-018) */
typedef struct {
    uint8_t ch[GX_NROLE];      /* MIDI channel 0..15 per role (drums: unused, see lane_ch) */
    uint8_t lane_ch[GX_NLANE]; /* MIDI channel per drum lane */
    uint8_t lane_note[GX_NLANE];
    const char *name;
} gx_outmap_t;

typedef enum { GX_MAP_TONVERK_SPLIT = 0, GX_MAP_GM, GX_NMAP } gx_map_id_t;

/* ---- API ---- */

/* a composition with every role on `genre`, all macros at 64, seeds derived from `seed` */
void gx_comp_init(gx_comp_t *c, gx_genre_t genre, uint32_t seed, uint16_t bpm_x10, uint8_t bars);

/* render every role of the composition. Returns 0, or -1 for an invalid composition. */
int gx_render(const gx_comp_t *c, const gx_outmap_t *map, gx_render_t *out);

/* sort events by time, then role, lane, note (stable, deterministic order) */
void gx_render_sort(gx_render_t *out);

/* for export: expand ratchets (rep > 1) into single hits and realise live chances (cond) per bar from
 * `seed` (one possible performance; the FM-1 plays both natively). 0, or -1 if `out` is too small.
 * `out` ends up sorted. */
int gx_render_expand(const gx_render_t *in, gx_render_t *out, uint32_t seed);

/* ---- performance operations (M4) ---- */

/* a new idea: every unlocked role gets a new seed (derived from `seed`) and loses its mutations */
void gx_compose(gx_comp_t *c, uint32_t seed);
/* lock or unlock a role. Locking the sub or the upper part snapshots what it depends on, so it keeps
 * its notes while the parts it listens to change. */
void gx_lock(gx_comp_t *c, gx_role_t role, int on);
/* re-seed one role (its mutations go); ignored if it is locked */
void gx_reseed(gx_comp_t *c, gx_role_t role, uint32_t seed);
/* add a mutation to a role: 0, -1 if the role is locked or already has GX_MAXMUT mutations */
int gx_mutate(gx_comp_t *c, gx_role_t role, uint8_t mask, uint8_t intensity, uint32_t seed);
/* similarity of one role between two renders: Jaccard of (lane, 16th, note) onsets, in 0..1000 */
uint32_t gx_similarity(const gx_render_t *a, const gx_render_t *b, gx_role_t role);

/* undo / redo: a ring of compositions (32: see docs/02-architecture.md §2b for the RAM budget) */
#ifndef GX_HIST
#define GX_HIST 32u            /* the firmware builds with -DGX_HIST=16 (RAM) */
#endif
typedef struct {
    gx_comp_t c[GX_HIST];
    uint32_t head;             /* the current entry */
    uint32_t n;                /* entries held (1..GX_HIST) */
    uint32_t redo;             /* entries ahead of head that redo can reach */
} gx_history_t;
void gx_hist_init(gx_history_t *h, const gx_comp_t *c);
void gx_hist_push(gx_history_t *h, const gx_comp_t *c);       /* after a change: drops the redo entries */
int gx_hist_undo(gx_history_t *h, gx_comp_t *c);              /* 1 = moved back (c updated), 0 = nothing */
int gx_hist_redo(gx_history_t *h, gx_comp_t *c);

/* parts (M5) */
#define GX_NPART 6u
enum { GX_PART_OPENING = 0, GX_PART_MAIN, GX_PART_VARIED, GX_PART_BREAKDOWN, GX_PART_RISING, GX_PART_PEAK };
/* a genre's name for a part slot ("BREAK", "TENSION"...) */
const char *gx_part_name(gx_genre_t g, uint32_t slot);

/* a genre's default tempo (BPM x 10); the test pattern's is 172 */
uint16_t gx_genre_tempo(gx_genre_t g);

const gx_outmap_t *gx_outmap(gx_map_id_t id);
const char *gx_genre_name(gx_genre_t g);
int gx_genre_from_name(const char *s);   /* -1 if unknown */

/* ---- randomness (gx_rng.c) ---- */
enum { GX_P_ONSET = 1, GX_P_VEL, GX_P_PITCH, GX_P_LEN, GX_P_TIME, GX_P_SEED,
       GX_P_VAR, GX_P_SPACE, GX_P_REP, GX_P_PICK, GX_P_COND };
uint32_t gx_hash(uint32_t seed, uint32_t role, uint32_t bar, uint32_t step, uint32_t purpose);
uint32_t gx_range(uint32_t h, uint32_t lo, uint32_t hi);     /* lo..hi inclusive, unbiased enough */
int gx_chance(uint32_t h, uint32_t p255);                    /* 1 with probability p255/256 */
uint32_t gx_role_seed(uint32_t master, gx_role_t role);

#endif
