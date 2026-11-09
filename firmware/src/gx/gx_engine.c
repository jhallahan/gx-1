/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Composition setup, output maps, the render entry point and the pipeline test pattern. */
#include "gx.h"
#include "gx_profile.h"

#include <stddef.h>

/* ---- output maps ---- */

/* Tonverk split (ADR-016/018): kicks on channel 1 (T1 Subtracks), kit on channel 2 (T2 Subtracks),
 * sub 5, chords 7, upper 8 (channels are 0-based here).
 * PLACEHOLDER notes: which note plays which Tonverk subtrack is test TV2 (still open);
 * middle C is 60 on the Tonverk (owner-confirmed), so subtracks are assumed at 60.. until measured. */
static const gx_outmap_t map_tonverk = {
    { 0, 4, 6, 7 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 60, 61, 60, 61, 62, 63, 64, 65, 66, 67 },
    "tonverk-split"
};

/* General MIDI / Ableton Drum Rack: all drums on channel 10 */
static const gx_outmap_t map_gm = {
    { 9, 0, 2, 4 },
    { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 },
    /* kick A, kick B (low kick), snare, ghost (side stick), clap, closed hat, open hat, ride, perc */
    { 36, 35, 38, 37, 39, 42, 46, 51, 75, 70 },
    "gm"
};

const gx_outmap_t *gx_outmap(gx_map_id_t id)
{
    switch (id) {
    case GX_MAP_TONVERK_SPLIT: return &map_tonverk;
    case GX_MAP_GM: return &map_gm;
    default: return NULL;
    }
}

/* ---- genres ---- */

static const char *const genre_names[GX_NGENRE] = { "test", "deep_dnb", "breaks", "autonomic", "dub_techno", "idm", "electro",
                                                     "uk_garage", "minimal", "downtempo", "jungle",
                                                     "halftime", "deep_house", "acid", "dubstep",
                                                     "liquid", "rollers", "detroit", "hard_techno", "lofi_house",
                                                     "ambient" };

const gx_genre_prof_t *gx_profile(gx_genre_t g)
{
    return (unsigned)g < GX_NGENRE ? gx_profiles[g] : NULL;
}

uint16_t gx_genre_tempo(gx_genre_t g)
{
    const gx_genre_prof_t *p = gx_profile(g);
    return p ? p->tempo_x10 : 1720;
}

const char *gx_genre_name(gx_genre_t g)
{
    return (unsigned)g < GX_NGENRE ? genre_names[g] : "?";
}

int gx_genre_from_name(const char *s)
{
    int g;
    for (g = 0; g < (int)GX_NGENRE; g++) {
        const char *a = genre_names[g], *b = s;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (!*a && !*b)
            return g;
    }
    return -1;
}

/* ---- composition ---- */

void gx_comp_init(gx_comp_t *c, gx_genre_t genre, uint32_t seed, uint16_t bpm_x10, uint8_t bars)
{
    int r, m;
    for (r = 0; r < (int)GX_NROLE; r++) {
        gx_recipe_t *p = &c->r[r];
        p->seed = gx_role_seed(seed, (gx_role_t)r);
        p->engine_ver = GX_VERSION;
        p->genre = (uint8_t)genre;
        for (m = 0; m < (int)GX_NMACRO; m++)
            p->macro[m] = 64;
        p->mut_n = 0;
        for (m = 0; m < (int)GX_MAXMUT; m++) {
            p->mut_mask[m] = p->mut_int[m] = 0;
            p->mut_seed[m] = 0;
        }
    }
    c->bpm_x10 = bpm_x10;
    c->key = gx_profile(genre) ? gx_profile(genre)->harm.key : 5;   /* the genre's home key (F if none) */
    c->bars = bars;
    c->lock = 0;
    c->part = GX_PART_MAIN;
    c->prog = 0;
    c->bpc = 0;
    c->lsub[0] = c->lsub[1] = c->r[GX_ROLE_SUB];
    c->lup[0] = c->lup[1] = c->lup[2] = c->lup[3] = c->r[GX_ROLE_UPPER];
}

/* ---- render helpers ---- */

static void emit(gx_render_t *o, uint32_t t, uint32_t len, gx_role_t role, uint8_t lane,
                 uint8_t note, uint8_t vel, uint8_t flags)
{
    gx_event_t *e;
    if (o->n >= o->cap) {
        o->dropped++;
        return;
    }
    if (vel < 1)
        vel = 1;
    if (vel > 127)
        vel = 127;
    if (len == 0)
        len = 1;
    e = &o->ev[o->n++];
    e->t = t;
    e->len = len;
    e->role = (uint8_t)role;
    e->lane = lane;
    e->note = note & 0x7f;
    e->vel = vel;
    e->flags = flags;
    e->rep = 1;
    e->cond = 0;
}

/* ---- the pipeline test pattern (GX_GENRE_TEST) ----
 * Deliberately simple and fixed, to prove determinism, mapping and export end to end.
 * It is not a genre: real profiles arrive in M2. */

static void test_drums(const gx_recipe_t *r, const gx_comp_t *c, const gx_outmap_t *map, gx_render_t *o)
{
    uint32_t bar, step;
    for (bar = 0; bar < c->bars; bar++) {
        for (step = 0; step < GX_BAR_STEPS; step++) {
            uint32_t t = bar * GX_BAR + step * GX_STEP;
            uint32_t hv = gx_hash(r->seed, GX_ROLE_DRUMS, bar, step, GX_P_VEL);
            uint8_t vjit = (uint8_t)gx_range(hv, 0, 12);
            if (step == 0)
                emit(o, t, GX_STEP, GX_ROLE_DRUMS, GX_LANE_KICK_A, map->lane_note[GX_LANE_KICK_A],
                     (uint8_t)(115 + vjit), GX_EF_ACCENT);
            if (step == 10)
                emit(o, t, GX_STEP, GX_ROLE_DRUMS, GX_LANE_KICK_B, map->lane_note[GX_LANE_KICK_B],
                     (uint8_t)(96 + vjit), 0);
            if (step == 4 || step == 12)
                emit(o, t, GX_STEP, GX_ROLE_DRUMS, GX_LANE_SNARE, map->lane_note[GX_LANE_SNARE],
                     (uint8_t)(110 + vjit), GX_EF_ACCENT);
            if ((step & 1u) == 0 &&
                gx_chance(gx_hash(r->seed, GX_ROLE_DRUMS, bar, step, GX_P_ONSET), 160))
                emit(o, t, GX_STEP / 2u, GX_ROLE_DRUMS, GX_LANE_CHAT, map->lane_note[GX_LANE_CHAT],
                     (uint8_t)(60 + vjit * 3u), 0);
        }
    }
}

static void test_sub(const gx_recipe_t *r, const gx_comp_t *c, gx_render_t *o)
{
    static const uint8_t degrees[4] = { 0, 0, 8, 10 };    /* root, root, b6, b7 (semitones) */
    uint32_t bar;
    for (bar = 0; bar < c->bars; bar++) {
        uint32_t h = gx_hash(r->seed, GX_ROLE_SUB, bar, 0, GX_P_PITCH);
        uint8_t note = (uint8_t)(24 + c->key + degrees[gx_range(h, 0, 3)]);  /* octave 1 */
        emit(o, bar * GX_BAR, 8u * GX_STEP, GX_ROLE_SUB, 0, note, 100, 0);
    }
}

/* working state for one render (static: no allocation): the drum context and harmony for the live parts,
 * and a second set for a locked part's snapshot context */
static gx_ctx_t ctx_buf, ctx_lock;
static gx_harmony_t harm_buf, harm_lock, harm_lock2;

static const gx_part_prof_t *part_of(const gx_genre_prof_t *p, const gx_comp_t *c)
{
    return &p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN];
}

/* a part's built-in variation (the "B" part): one extra mutation from a fixed seed, after the recipe's own */
static void vary_into(const gx_recipe_t *r, gx_role_t role, const gx_comp_t *c, const gx_genre_prof_t *p,
                      const gx_harmony_t *h, gx_render_t *o, uint32_t s)
{
    const gx_part_prof_t *pt = part_of(p, c);
    gx_recipe_t v;
    if (!pt->vary)
        return;
    v = *r;
    v.mut_n = 1;
    v.mut_mask[0] = (uint8_t)(role == GX_ROLE_DRUMS ? (GX_MUT_RHYTHM | GX_MUT_VEL) : (GX_MUT_RHYTHM | GX_MUT_PITCH | GX_MUT_VEL));
    v.mut_int[0] = pt->vary;
    v.mut_seed[0] = r->seed ^ 0x5bd1e995u;
    gx_mutate_apply(&v, role, c, p, h, o, s);
}

/* the CC lane (M5): a filter drift, a sweep up (builds), open (drops) or closed, per the part playing */
static void cc_lane(const gx_comp_t *c, const gx_genre_prof_t *p, uint32_t seed, gx_render_t *o)
{
    uint32_t mode = part_of(p, c)->cc, n = (uint32_t)c->bars * 8u, k, phase = gx_hash(seed, 77, 0, 0, GX_P_PICK) % n;
    for (k = 0; k < n; k++) {
        int32_t v;
        gx_event_t *e;
        if (mode == GX_CC_NONE)
            return;
        if (mode == GX_CC_OPEN || mode == GX_CC_CLOSED) {
            if (k)
                return;
            v = mode == GX_CC_OPEN ? 127 : 34;
        } else if (mode == GX_CC_SWEEP) {
            uint32_t x = n > 1u ? k * 1000u / (n - 1u) : 1000u;
            v = 20 + (int32_t)(107u * x / 1000u * x / 1000u);    /* eases in: the last bars open fastest */
        } else {
            uint32_t pos = (k + phase) % n, tri = pos < n / 2u ? pos * 2000u / n : (n - pos) * 2000u / n;
            v = 40 + (int32_t)(56u * tri / 1000u);
        }
        if (o->n >= o->cap) {
            o->dropped++;
            return;
        }
        e = &o->ev[o->n++];
        e->t = k * 2u * GX_STEP;
        e->len = 1;
        e->role = (uint8_t)GX_ROLE_CC;
        e->lane = p->cc_target;
        e->note = p->cc_number;
        e->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
        e->flags = 0;
        e->rep = 1;
        e->cond = 0;
    }
}

const char *gx_part_name(gx_genre_t g, uint32_t slot)
{
    const gx_genre_prof_t *p = gx_profile(g);
    if (slot >= GX_NPART)
        return "";
    return p ? p->part[slot].name : (slot == GX_PART_MAIN ? "MAIN" : "");
}

/* drums from a recipe (with its mutations), appended to o */
static void drums_into(const gx_recipe_t *r, const gx_comp_t *c, const gx_outmap_t *map, gx_render_t *o)
{
    const gx_genre_prof_t *p = gx_profile((gx_genre_t)r->genre);
    uint32_t s = o->n;
    if (!p)
        return;
    gx_drums_render(r, c, p, map, o);
    gx_mutate_apply(r, GX_ROLE_DRUMS, c, p, NULL, o, s);
    vary_into(r, GX_ROLE_DRUMS, c, p, NULL, o, s);
}

/* the sub from a recipe, against a context and harmony (with its mutations), appended to o */
static void sub_into(const gx_recipe_t *r, const gx_comp_t *c, const gx_harmony_t *h, const gx_ctx_t *x,
                     gx_render_t *o)
{
    const gx_genre_prof_t *p = gx_profile((gx_genre_t)r->genre);
    uint32_t s = o->n;
    if (!p)
        return;
    gx_sub_render(r, c, p, h, x, o);
    gx_mutate_apply(r, GX_ROLE_SUB, c, p, h, o, s);
    vary_into(r, GX_ROLE_SUB, c, p, h, o, s);
}

/* a context from the events [s, o->n) */
static void ctx_from(const gx_render_t *o, uint32_t s, uint32_t bars, gx_ctx_t *x)
{
    gx_render_t v;
    v.ev = o->ev + s;
    v.n = o->n - s;
    v.cap = v.n;
    v.dropped = 0;
    gx_ctx_build(&v, bars, x);
}

static void harmony_from(const gx_recipe_t *chords, const gx_comp_t *c, gx_harmony_t *h)
{
    const gx_genre_prof_t *p = gx_profile((gx_genre_t)chords->genre);
    if (p)
        gx_harmony_build(chords, c, p, h);
}

int gx_render(const gx_comp_t *c, const gx_outmap_t *map, gx_render_t *out)
{
    const gx_genre_prof_t *pr[GX_NROLE];
    int r;
    if (!c || !map || !out || c->bars < 1 || c->bars > GX_MAXBARS || c->key > 11)
        return -1;
    out->n = 0;
    out->dropped = 0;
    for (r = 0; r < (int)GX_NROLE; r++) {
        if (c->r[r].genre >= GX_NGENRE)
            return -1;
        pr[r] = gx_profile((gx_genre_t)c->r[r].genre);
    }
    /* the test pattern: fixed drums and sub, no dependencies */
    if (c->r[GX_ROLE_DRUMS].genre == GX_GENRE_TEST)
        test_drums(&c->r[GX_ROLE_DRUMS], c, map, out);
    else
        drums_into(&c->r[GX_ROLE_DRUMS], c, map, out);
    if (c->r[GX_ROLE_SUB].genre == GX_GENRE_TEST)
        test_sub(&c->r[GX_ROLE_SUB], c, out);

    /* the dependency chain: drums -> context; chords recipe -> harmony; then chords, sub, upper */
    gx_ctx_build(out, c->bars, &ctx_buf);
    if (pr[GX_ROLE_CHORDS])
        gx_harmony_build(&c->r[GX_ROLE_CHORDS], c, pr[GX_ROLE_CHORDS], &harm_buf);
    else if (pr[GX_ROLE_SUB])
        gx_harmony_build(&c->r[GX_ROLE_CHORDS], c, pr[GX_ROLE_SUB], &harm_buf);
    if (pr[GX_ROLE_CHORDS]) {
        uint32_t s = out->n;
        gx_chords_render(&c->r[GX_ROLE_CHORDS], c, pr[GX_ROLE_CHORDS], &harm_buf, out);
        gx_mutate_apply(&c->r[GX_ROLE_CHORDS], GX_ROLE_CHORDS, c, pr[GX_ROLE_CHORDS], &harm_buf, out, s);
    }

    if (pr[GX_ROLE_SUB]) {
        if ((c->lock >> GX_ROLE_SUB) & 1u) {
            /* a locked sub keeps its notes: render it against the context it was locked with. The snapshot
             * drums go into the buffer's free tail, become a context, and are dropped again. */
            uint32_t s = out->n;
            drums_into(&c->lsub[0], c, map, out);
            ctx_from(out, s, c->bars, &ctx_lock);
            out->n = s;
            harmony_from(&c->lsub[1], c, &harm_lock);
            sub_into(&c->r[GX_ROLE_SUB], c, &harm_lock, &ctx_lock, out);
        } else {
            sub_into(&c->r[GX_ROLE_SUB], c, &harm_buf, &ctx_buf, out);
        }
    }

    if (pr[GX_ROLE_UPPER]) {
        const gx_recipe_t *ur = &c->r[GX_ROLE_UPPER];
        uint32_t s = out->n;
        if ((c->lock >> GX_ROLE_UPPER) & 1u) {
            /* a locked upper part: the sub it heard (its drums and chords) and the harmony it followed */
            gx_render_t view;
            uint32_t u0, k;
            drums_into(&c->lup[0], c, map, out);
            ctx_from(out, s, c->bars, &ctx_lock);
            out->n = s;
            harmony_from(&c->lup[1], c, &harm_lock);
            sub_into(&c->lup[3], c, &harm_lock, &ctx_lock, out);       /* the sub it heard, in the tail */
            harmony_from(&c->lup[2], c, &harm_lock2);
            view.ev = out->ev + s;
            view.n = out->n - s;
            view.cap = view.n;
            view.dropped = 0;
            u0 = out->n;
            gx_upper_render(ur, c, pr[GX_ROLE_UPPER], &harm_lock2, &ctx_lock, &view, out);
            gx_mutate_apply(ur, GX_ROLE_UPPER, c, pr[GX_ROLE_UPPER], &harm_lock2, out, u0);
            vary_into(ur, GX_ROLE_UPPER, c, pr[GX_ROLE_UPPER], &harm_lock2, out, u0);
            for (k = u0; k < out->n; k++)                             /* drop the tail, keep the upper part */
                out->ev[s + (k - u0)] = out->ev[k];
            out->n = s + (out->n - u0);
        } else {
            gx_upper_render(ur, c, pr[GX_ROLE_UPPER], &harm_buf, &ctx_buf, out, out);
            gx_mutate_apply(ur, GX_ROLE_UPPER, c, pr[GX_ROLE_UPPER], &harm_buf, out, s);
            vary_into(ur, GX_ROLE_UPPER, c, pr[GX_ROLE_UPPER], &harm_buf, out, s);
        }
    }
    if (pr[GX_ROLE_UPPER])
        cc_lane(c, pr[GX_ROLE_UPPER], c->r[GX_ROLE_UPPER].seed, out);
    gx_render_sort(out);
    return 0;
}

/* insertion sort: render sizes are small (hundreds of events) and it is stable */
static int ev_before(const gx_event_t *a, const gx_event_t *b)
{
    if (a->t != b->t) return a->t < b->t;
    if (a->role != b->role) return a->role < b->role;
    if (a->lane != b->lane) return a->lane < b->lane;
    return a->note < b->note;
}

void gx_render_sort(gx_render_t *out)
{
    uint32_t i, j;
    for (i = 1; i < out->n; i++) {
        gx_event_t e = out->ev[i];
        j = i;
        while (j > 0 && ev_before(&e, &out->ev[j - 1])) {
            out->ev[j] = out->ev[j - 1];
            j--;
        }
        out->ev[j] = e;
    }
}

int gx_render_expand(const gx_render_t *in, gx_render_t *out, uint32_t seed)
{
    uint32_t i, k;
    out->n = 0;
    out->dropped = 0;
    for (i = 0; i < in->n; i++) {
        const gx_event_t *e = &in->ev[i];
        uint32_t r = e->rep > 1 ? e->rep : 1u, sub = e->len / r;
        if (e->cond && e->cond <= 100u) {          /* this performance: does the live chance fire? */
            uint32_t st = e->t / GX_STEP;
            if (!gx_chance(gx_hash(seed, (uint32_t)e->role * 16u + e->lane, st / 16u, st % 16u, GX_P_COND),
                           (uint32_t)e->cond * 256u / 100u))
                continue;
        }
        if (sub == 0)
            sub = 1;
        for (k = 0; k < r; k++) {
            gx_event_t *d;
            if (out->n >= out->cap)
                return -1;
            d = &out->ev[out->n++];
            *d = *e;
            d->rep = 1;
            d->cond = 0;
            d->t = e->t + k * sub;
            d->len = r > 1 ? (sub > 1 ? sub / 2u + sub / 4u : 1u) : e->len;   /* ratchet hits: 3/4 gate */
            if (k)                                     /* each repeat a little softer */
                d->vel = (uint8_t)(e->vel > 8u * k + 1u ? e->vel - 8u * k : 1u);
        }
    }
    gx_render_sort(out);
    return 0;
}
