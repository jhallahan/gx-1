/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The pitched parts (M3): a harmony timeline, voice-led chords, a sub bass that listens to the drums,
 * and an upper part (arp, mid bass or motif) that follows the chords.
 *
 * Dependencies (docs/02-architecture.md §3.3): the drums are rendered first and summarised in a context
 * (kick, snare and activity per 16th); the harmony comes from the chords recipe; the sub reads the
 * context and the harmony; the upper part reads the harmony, the context and the sub it answers.
 * Each part applies its *own* genre's rules (the profile of the role), which is how blends stay coherent. */
#include "gx_profile.h"

#define ID(role, k) ((uint32_t)(role) * 16u + (uint32_t)(k))
#define HARM_ID ID(GX_ROLE_CHORDS, 14)

static const uint8_t SCALE[GX_NSCALE][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },  /* aeolian */
    { 0, 2, 3, 5, 7, 9, 10 },  /* dorian */
    { 0, 1, 3, 5, 7, 8, 10 },  /* phrygian */
    { 0, 2, 4, 5, 7, 9, 11 },  /* ionian (major: pop, R&B) */
};

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t mdelta(const gx_recipe_t *r, gx_macro_t m) { return (int32_t)r->macro[m] - 64; }

/* Tension and Complex: the knob, plus the part's lean (a build leans in, an intro is plainer); -64..63 */
static const gx_part_prof_t *part_of(const gx_genre_prof_t *p, const gx_comp_t *c)
{
    return &p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN];
}
static int32_t tension_of(const gx_recipe_t *r, const gx_genre_prof_t *p, const gx_comp_t *c)
{
    return clampi(mdelta(r, GX_M_TENSION) + part_of(p, c)->tension, -64, 63);
}
static int32_t complex_of(const gx_recipe_t *r, const gx_genre_prof_t *p, const gx_comp_t *c)
{
    return clampi(mdelta(r, GX_M_COMPLEX) + part_of(p, c)->complex, -64, 63);
}
/* Complex on a step's weight: the off-16ths (odd steps) get busier or plainer; on-steps are left alone */
static int32_t complex_w(int32_t w, uint32_t step, int32_t dc)
{
    return (step & 1u) ? w * (64 + dc) / 64 : w;
}

/* semitones of a scale degree (any octave) above the key */
static uint32_t deg_semi(uint32_t scale, uint32_t deg) { return SCALE[scale][deg % 7u] + 12u * (deg / 7u); }

/* a weighted pick among n weights from a hash; -1 if all are 0 */
static int pick(uint32_t h, const uint8_t *w, int n)
{
    uint32_t tot = 0, x;
    int i;
    for (i = 0; i < n; i++)
        tot += w[i];
    if (!tot)
        return -1;
    x = gx_range(h, 0, tot - 1u);
    for (i = 0; i < n; i++) {
        if (x < w[i])
            return i;
        x -= w[i];
    }
    return n - 1;
}

/* the bar a decision is hashed on: the 2-bar cycle's, unless Evolution re-rolls it (as the drums do) */
static uint32_t dbar(uint32_t seed, uint32_t id, uint32_t bar, uint32_t step, uint32_t var)
{
    if (bar < 2u)
        return bar;
    if (gx_chance(gx_hash(seed, id, bar, step, GX_P_VAR), var))
        return bar;
    return bar & 1u;
}

static uint32_t evolve_var(const gx_recipe_t *r, const gx_genre_prof_t *p)
{
    return (uint32_t)clampi((int32_t)p->var * (64 + mdelta(r, GX_M_EVOLVE)) / 64, 0, 255);
}

/* swing on the odd 16ths, the drums' feel (Groove) */
static int32_t swing_units(const gx_recipe_t *r, const gx_genre_prof_t *p, uint32_t step)
{
    int32_t sw = clampi((int32_t)p->swing + mdelta(r, GX_M_GROOVE) * (int32_t)p->swing_range / 64, 50, 75);
    return (step & 1u) ? (sw * 2 - 100) * (int32_t)GX_STEP / 100 : 0;
}

static void put(gx_render_t *o, uint32_t t, uint32_t len, gx_role_t role, uint8_t lane, int32_t note,
                int32_t vel, uint8_t flags)
{
    gx_event_t *e;
    if (o->n >= o->cap) {
        o->dropped++;
        return;
    }
    e = &o->ev[o->n++];
    e->t = t;
    e->len = len ? len : 1u;
    e->role = (uint8_t)role;
    e->lane = lane;
    e->note = (uint8_t)clampi(note, 0, 127);
    e->vel = (uint8_t)clampi(vel, 1, 127);
    e->flags = flags;
    e->rep = 1;
    e->cond = 0;
}

/* ---- context ---- */

void gx_ctx_build(const gx_render_t *drums, uint32_t bars, gx_ctx_t *ctx)
{
    uint32_t i, n = bars * 16u;
    for (i = 0; i < GX_MAXBARS * 16u; i++)
        ctx->kick[i] = ctx->snare[i] = ctx->act[i] = 0;
    for (i = 0; i < drums->n; i++) {
        const gx_event_t *e = &drums->ev[i];
        uint32_t st = (e->t + GX_STEP / 2u) / GX_STEP;
        if (e->role != GX_ROLE_DRUMS || st >= n)
            continue;
        if (e->lane == GX_LANE_KICK_A || e->lane == GX_LANE_KICK_B) {
            if (e->vel > ctx->kick[st])
                ctx->kick[st] = e->vel;
        } else if (e->lane == GX_LANE_SNARE) {
            ctx->snare[st] = e->vel;
        }
        if (ctx->act[st] < 255u && !e->cond)
            ctx->act[st]++;
    }
}

/* ---- harmony ---- */

static uint16_t chord_pcs(uint32_t key, uint32_t scale, uint32_t d, uint32_t col)
{
    uint32_t tones[5], n = 0, i;
    uint16_t m = 0;
    tones[n++] = d;
    tones[n++] = col == GX_COL_SUS2 ? d + 1u : d + 2u;
    tones[n++] = d + 4u;
    if (col == GX_COL_SEVENTH)
        tones[n++] = d + 6u;
    else if (col == GX_COL_ADD9)
        tones[n++] = d + 1u;
    else if (col == GX_COL_ELEVENTH) {
        tones[n++] = d + 6u;
        tones[n++] = d + 3u;
    }
    for (i = 0; i < n; i++)
        m |= (uint16_t)(1u << ((key + deg_semi(scale, tones[i])) % 12u));
    return m;
}

void gx_harmony_build(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, gx_harmony_t *h)
{
    const gx_harm_prof_t *H = &p->harm;
    uint32_t seed = r->seed, bar;
    /* the progression and the bars per chord: chosen by the player (c->prog, c->bpc), or by the idea */
    uint32_t pi = c->prog && c->prog <= H->nprog ? c->prog - 1u
                  : gx_range(gx_hash(seed, HARM_ID, 0, 0, GX_P_PICK), 0, H->nprog ? H->nprog - 1u : 0u);
    uint32_t bpc = c->bpc ? c->bpc
                   : H->nbpc ? H->bpc[gx_range(gx_hash(seed, HARM_ID, 0, 1, GX_P_PICK), 0, H->nbpc - 1u)] : 2u;
    uint32_t len = H->prog_len[pi] ? H->prog_len[pi] : 1u;
    if (!bpc)
        bpc = 2;
    h->prog = (uint8_t)pi;
    h->bpc = (uint8_t)bpc;
    /* Tension: plain triads give way to sevenths, add9s, sus2s and elevenths (and back the other way) */
    const int32_t dt = tension_of(r, p, c);
    uint8_t cw[GX_NCOL];
    uint32_t i;
    for (i = 0; i < GX_NCOL; i++) {
        int32_t w = H->colour[i];
        if (i == GX_COL_TRIAD)
            w = w * (64 - dt) / 64;
        else
            w = w * (64 + dt) / 64 + (dt >= 32 ? 1 : 0);
        cw[i] = (uint8_t)clampi(w, 0, 255);
    }
    for (bar = 0; bar < GX_MAXBARS; bar++) {
        uint32_t k = bar / bpc, slot = k % len, d = H->prog[pi][slot];
        int col = pick(gx_hash(seed, HARM_ID, slot, 2, GX_P_PICK), cw, GX_NCOL);
        if (col < 0)
            col = GX_COL_TRIAD;
        h->deg[bar] = (uint8_t)d;
        h->root[bar] = (uint8_t)((c->key + deg_semi(H->scale, d)) % 12u);
        h->col[bar] = (uint8_t)col;
        h->pcs[bar] = chord_pcs(c->key, H->scale, d, (uint32_t)col);
        h->change[bar] = (uint8_t)(bar % bpc == 0);
    }
}

/* the lowest note >= lo with pitch class pc */
static int32_t lowest(uint32_t lo, uint32_t pc)
{
    uint32_t n = lo;
    while (n % 12u != pc)
        n++;
    return (int32_t)n;
}

/* ---- chords: voicing and voice leading ---- */

static uint32_t voice(const gx_chord_prof_t *C, uint16_t pcs, uint32_t root, const int32_t *prev, uint32_t nprev,
                      int32_t *out)
{
    int32_t cand[64];
    uint32_t nc = 0, n, i, j, nv = C->voices;
    uint8_t used[64];
    for (n = C->lo; n <= C->hi && nc < 64u; n++)
        if ((pcs >> (n % 12u)) & 1u)
            cand[nc++] = (int32_t)n;
    if (!nc)
        return 0;
    for (i = 0; i < nc; i++)
        used[i] = 0;
    if (!nprev) {                              /* first chord: stack up from the root */
        int32_t r0 = lowest(C->lo, root);
        uint32_t k = 0;
        for (i = 0; i < nc && k < nv; i++)
            if (cand[i] >= r0) {
                out[k++] = cand[i];
                used[i] = 1;
            }
        return k;
    }
    for (j = 0; j < nv && j < nprev; j++) {    /* each voice moves to the nearest free chord tone */
        uint32_t best = nc;
        int32_t bd = 1000;
        for (i = 0; i < nc; i++) {
            int32_t d = cand[i] - prev[j];
            if (d < 0) d = -d;
            if (!used[i] && d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best == nc)
            break;
        used[best] = 1;
        out[j] = cand[best];
    }
    n = j;
    /* keep the root in the voicing: the lowest voice takes the nearest root if none has it */
    for (i = 0; i < n; i++)
        if ((uint32_t)out[i] % 12u == root)
            break;
    if (i == n && n) {
        uint32_t lo_i = 0;
        int32_t best = -1, bd = 1000;
        for (i = 1; i < n; i++)
            if (out[i] < out[lo_i]) lo_i = i;
        for (i = 0; i < nc; i++) {
            int32_t d = cand[i] - out[lo_i];
            if (d < 0) d = -d;
            if ((uint32_t)cand[i] % 12u == root && d < bd) {
                bd = d;
                best = cand[i];
            }
        }
        if (best >= 0)
            out[lo_i] = best;
    }
    /* sort ascending; drop doubled notes */
    for (i = 1; i < n; i++) {
        int32_t v = out[i];
        j = i;
        while (j > 0 && out[j - 1] > v) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    for (i = 1, j = 1; i < n; i++)
        if (out[i] != out[j - 1])
            out[j++] = out[i];
    return n ? j : 0;
}

void gx_chords_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                      gx_render_t *o)
{
    const gx_chord_prof_t *C = &p->chords;
    const uint32_t seed = r->seed, end = (uint32_t)c->bars * GX_BAR;
    const int32_t dd = mdelta(r, GX_M_DENSITY), dc = complex_of(r, p, c);
    const uint32_t var = evolve_var(r, p);
    int stab = gx_chance(gx_hash(seed, ID(GX_ROLE_CHORDS, 0), 0, 0, GX_P_PICK), C->stab_p);
    const gx_part_prof_t *pt = &p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN];
    int32_t prev[5], cur[5];
    uint32_t nprev = 0, ncur = 0, bar, k;
    int32_t starts[GX_MAXBARS + 1];
    uint32_t nspan = 0, span_bar[GX_MAXBARS + 1];

    if (!C->voices || pt->chords == GX_PC_OFF)
        return;
    if (pt->chords == GX_PC_PAD)
        stab = 0;
    /* the chord spans and their start times (pads may anticipate a change by a 16th or two) */
    for (bar = 0; bar < c->bars; bar++)
        if (bar == 0 || h->change[bar]) {
            int32_t t = (int32_t)(bar * GX_BAR);
            if (bar && !stab && gx_chance(gx_hash(seed, ID(GX_ROLE_CHORDS, 1), bar, 0, GX_P_TIME), C->antic))
                t -= (int32_t)GX_STEP * (int32_t)gx_range(gx_hash(seed, ID(GX_ROLE_CHORDS, 1), bar, 1, GX_P_TIME), 1, 2);
            starts[nspan] = t;
            span_bar[nspan++] = bar;
        }
    starts[nspan] = (int32_t)end;

    for (k = 0; k < nspan; k++) {
        uint32_t b0 = span_bar[k], b1 = k + 1 < nspan ? span_bar[k + 1] : c->bars, i;
        ncur = voice(C, h->pcs[b0], h->root[b0], prev, nprev, cur);
        if (!ncur)
            continue;
        if (!stab) {                           /* a held pad over the span */
            uint32_t t = (uint32_t)starts[k], len = (uint32_t)(starts[k + 1] - starts[k]);
            int32_t vel = (int32_t)gx_range(gx_hash(seed, ID(GX_ROLE_CHORDS, 2), b0, 0, GX_P_VEL), C->vel_lo, C->vel_hi);
            len = len > GX_STEP / 2u ? len - GX_STEP / 4u : len;
            for (i = 0; i < ncur; i++)
                put(o, t, len, GX_ROLE_CHORDS, (uint8_t)i, cur[i], vel + (i + 1 == ncur ? 4 : 0), 0);
        } else {                               /* rhythmic stabs inside the span */
            for (bar = b0; bar < b1; bar++) {
                uint32_t step;
                for (step = 0; step < 16u; step++) {
                    int32_t w = complex_w((int32_t)C->w[step] * (6400 + dd * 100) / 6400, step, dc);
                    uint32_t db = dbar(seed, ID(GX_ROLE_CHORDS, 3), bar, step, var);
                    int32_t vel, t;
                    if (w <= 0 || !gx_chance(gx_hash(seed, ID(GX_ROLE_CHORDS, 3), db, step, GX_P_ONSET), (uint32_t)clampi(w, 0, 255)))
                        continue;
                    vel = (int32_t)gx_range(gx_hash(seed, ID(GX_ROLE_CHORDS, 3), db, step, GX_P_VEL), C->vel_lo, C->vel_hi);
                    t = (int32_t)(bar * GX_BAR + step * GX_STEP) + swing_units(r, p, step);
                    for (i = 0; i < ncur; i++)
                        put(o, (uint32_t)clampi(t, 0, (int32_t)end - 1), (uint32_t)C->gate * GX_STEP - GX_STEP / 4u,
                            GX_ROLE_CHORDS, (uint8_t)i, cur[i], vel + (i + 1 == ncur ? 4 : 0), 0);
                }
            }
        }
        for (i = 0; i < ncur; i++)
            prev[i] = cur[i];
        nprev = ncur;
    }
}

/* ---- sub bass ---- */

/* the note for a chord root and a move (root, fifth, octave, seventh), inside the register */
static int32_t bass_note(uint32_t lo, uint32_t hi, uint32_t root, int move)
{
    int32_t base = lowest(lo, root), n = base;
    if (move == 1) n = base + 7;
    else if (move == 2) n = base + 12;
    else if (move == 3) n = base + 10;
    if (n > (int32_t)hi)
        n -= 12;
    if (n < (int32_t)lo)
        n = base;
    return n;
}

void gx_sub_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                   const gx_ctx_t *ctx, gx_render_t *o)
{
    const gx_sub_prof_t *S = &p->sub;
    const uint32_t seed = r->seed, nsteps = (uint32_t)c->bars * 16u, end = (uint32_t)c->bars * GX_BAR;
    const int32_t dd = mdelta(r, GX_M_DENSITY), dsp = mdelta(r, GX_M_SPACE);
    const int32_t dt = tension_of(r, p, c), dc = complex_of(r, p, c);
    const uint32_t var = evolve_var(r, p), id = ID(GX_ROLE_SUB, 0);
    uint8_t mvw[4];                            /* Tension: fewer roots, more sevenths */
    mvw[0] = (uint8_t)clampi((int32_t)S->move[0] * (128 - dt) / 128, 0, 255);
    mvw[1] = S->move[1];
    mvw[2] = S->move[2];
    mvw[3] = (uint8_t)clampi((int32_t)S->move[3] * (64 + dt) / 64 + (dt >= 32 ? 1 : 0), 0, 255);
    uint8_t on[GX_MAXBARS * 16u];
    uint32_t i, bar;
    int32_t last_root = -1;

    const gx_part_prof_t *pt = &p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN];
    const uint32_t silent_end = (pt->silent_last && c->bars > 1u) ? nsteps - 16u : nsteps;
    if (!S->lo || pt->sub == GX_PS_OFF)
        return;
    if (pt->sub == GX_PS_LONG) {               /* held roots, one per chord (breakdowns) */
        for (bar = 0; bar < c->bars; bar++) {
            uint32_t b1 = bar + 1u, t0 = bar * GX_BAR, t1;
            if (bar && !h->change[bar])
                continue;
            while (b1 < c->bars && !h->change[b1])
                b1++;
            t1 = b1 * GX_BAR < silent_end * GX_STEP ? b1 * GX_BAR : silent_end * GX_STEP;
            if (t1 > t0 + GX_STEP)
                put(o, t0, t1 - t0 - GX_STEP / 8u, GX_ROLE_SUB, 0, bass_note(S->lo, S->hi, h->root[bar], 0),
                    (S->vel_lo + S->vel_hi) / 2, GX_EF_ANCHOR);
        }
        return;
    }
    for (i = 0; i < nsteps; i++)
        on[i] = 0;
    for (bar = 0; bar < c->bars; bar++) {
        uint32_t step, count = 0;
        uint8_t wbar[16];
        for (step = 0; step < 16u; step++) {
            uint32_t cs = (bar & 1u) * 16u + step, idx = bar * 16u + step, a = 0;
            int anchor = (int)((S->anchor >> cs) & 1u);
            int32_t w = S->w[cs];
            /* a reinforcing bass states each new chord on its first beat */
            if (step == 0 && h->change[bar] && S->at_kick >= 100)
                anchor = 1;
            if (ctx->kick[idx])
                w = w * S->at_kick / 100;
            else if ((idx >= 1 && ctx->kick[idx - 1]) || (idx >= 2 && ctx->kick[idx - 2]))
                w = w * S->after_kick / 100;
            if (ctx->snare[idx])
                w = w * S->at_snare / 100;
            a += ctx->act[idx];
            if (idx) a += ctx->act[idx - 1];
            if (idx + 1 < nsteps) a += ctx->act[idx + 1];
            w = w * (300 + (int32_t)S->dens_follow * (int32_t)a) / 300;
            w = w * (6400 + dd * 100) / 6400;
            w = w * (128 - dsp) / 128;          /* Space: fewer bass notes, longer silences */
            w = complex_w(w, step, dc / 2);     /* Complex: more (or fewer) off-16ths, gently */
            w = clampi(w, 0, 255);
            wbar[step] = (uint8_t)w;
            if (anchor || gx_chance(gx_hash(seed, id, dbar(seed, id, bar, step, var), step, GX_P_ONSET), (uint32_t)w)) {
                on[idx] = (uint8_t)(anchor ? 2 : 1);
                count++;
            }
        }
        while (S->max && count > S->max) {     /* the cap: drop the weakest non-anchor onset */
            uint32_t best = 16, bw = 1000;
            for (step = 0; step < 16u; step++)
                if (on[bar * 16u + step] == 1 && wbar[step] < bw) {
                    bw = wbar[step];
                    best = step;
                }
            if (best == 16)
                break;
            on[bar * 16u + best] = 0;
            count--;
        }
    }

    for (i = silent_end; i < nsteps; i++)
        on[i] = 0;                             /* the part's silent last bar */
    for (i = 0; i < nsteps; i++) {
        uint32_t j, gap, len, b = i / 16u, step = i % 16u, db = dbar(seed, id, b, step, var);
        int move, root = h->root[b];
        int32_t note, t, vel;
        uint8_t flags = 0;
        if (!on[i])
            continue;
        for (j = i + 1; j < nsteps && !on[j]; j++)
            ;
        gap = j - i;
        len = gx_range(gx_hash(seed, id, db, step, GX_P_LEN), S->len_min, S->len_max);
        if (gx_chance(gx_hash(seed, id, db, step, GX_P_REP), S->legato) || len > gap)
            len = gap;
        move = (root != last_root) ? 0 : pick(gx_hash(seed, id, db, step, GX_P_PITCH), mvw, 4);
        if (move < 0)
            move = 0;
        note = bass_note(S->lo, S->hi, (uint32_t)root, move);
        last_root = root;
        vel = (int32_t)gx_range(gx_hash(seed, id, db, step, GX_P_VEL), S->vel_lo, S->vel_hi);
        if (gx_chance(gx_hash(seed, id, db, step, GX_P_TIME), S->glide))
            flags |= GX_EF_GLIDE;
        t = (int32_t)(i * GX_STEP) + swing_units(r, p, step);
        t = clampi(t, 0, (int32_t)end - 1);
        len = len * GX_STEP;
        if ((uint32_t)t + len > silent_end * GX_STEP)
            len = silent_end * GX_STEP > (uint32_t)t ? silent_end * GX_STEP - (uint32_t)t : 1u;
        if (len > GX_STEP / 2u && j < nsteps)
            len -= GX_STEP / 8u;               /* a hair of release before the next note */
        put(o, (uint32_t)t, len, GX_ROLE_SUB, 0, note, vel, flags);
    }
}

/* ---- upper: arp, mid bass or motif ---- */

void gx_upper_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p, const gx_harmony_t *h,
                     const gx_ctx_t *ctx, const gx_render_t *sofar, gx_render_t *o)
{
    const gx_upper_prof_t *U = &p->upper;
    const uint32_t seed = r->seed, end = (uint32_t)c->bars * GX_BAR, id = ID(GX_ROLE_UPPER, 0);
    const int32_t dd = mdelta(r, GX_M_DENSITY), dt = tension_of(r, p, c), dc = complex_of(r, p, c);
    /* Tension: the chance of a colour tone; Complex: the motif's interval range and octave leaps */
    const uint32_t ten = (uint32_t)clampi((int32_t)U->tension * (64 + dt) / 64 + (dt >= 32 ? dt : 0), 0, 255);
    const uint32_t mlo = dc < 0 ? (uint32_t)clampi(-dc / 24, 0, 2) : 0u;
    const uint32_t leap = dc > 0 ? (uint32_t)(dc * 2) : 0u;
    const uint32_t var = evolve_var(r, p);
    uint8_t subon[GX_MAXBARS * 16u];
    uint32_t i, bar, k = 0;
    int32_t walk = 0;

    (void)ctx;
    if (U->mode == GX_UPPER_NONE || !p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN].upper)
        return;
    for (i = 0; i < GX_MAXBARS * 16u; i++)
        subon[i] = 0;
    for (i = 0; i < sofar->n; i++)             /* the sub's attacks (the mid bass answers around them) */
        if (sofar->ev[i].role == GX_ROLE_SUB) {
            uint32_t s = (sofar->ev[i].t + GX_STEP / 2u) / GX_STEP;
            if (s < GX_MAXBARS * 16u) subon[s] = 1;
            if (s + 1u < GX_MAXBARS * 16u) subon[s + 1u] = 1;
        }

    int32_t mwalk = 0;                         /* the motif's pitch, walked across a 2-bar cell */
    int32_t forced = -1;                       /* the motif's cell step played even if no chance fires */
    for (bar = 0; bar < c->bars; bar++) {
        int32_t tones[24];
        uint32_t nt = 0, n, step;
        if (U->mode == GX_UPPER_MOTIF && (bar & 1u) == 0u) {
            /* a new 2-bar cell: the phrase starts from its own point in the register (the odd cells from another:
               call and answer, A B A B), and the cell never stays silent (if no slot's chance fires, the
               strongest slot plays) */
            uint32_t cs, best = 0;
            int any = 0;
            int32_t span = (int32_t)U->hi - (int32_t)U->lo;
            uint32_t d0 = dbar(seed, id, bar, 0, var);
            mwalk = (int32_t)U->lo + span / 8 +
                    (int32_t)gx_range(gx_hash(seed, id, d0, (bar / 2u) & 1u, GX_P_PICK), 0, (uint32_t)(span * 3 / 4));
            forced = -1;
            for (cs = 0; cs < 32u && bar + cs / 16u < c->bars; cs++) {
                int32_t w = U->w[cs];
                if (!w)
                    continue;
                w = clampi(w * (6400 + dd * 100) / 6400, 0, 255);
                if (gx_chance(gx_hash(seed, id, dbar(seed, id, bar + cs / 16u, cs % 16u, var), cs % 16u, GX_P_ONSET), (uint32_t)w))
                    any = 1;
                if (U->w[cs] > U->w[best])
                    best = cs;
            }
            if (!any && U->w[best])
                forced = (int32_t)best;
        }
        uint16_t pcs = h->pcs[bar];
        /* the arp's colour: the 9th joins the chord tones for this chord (Tension) */
        if (U->mode == GX_UPPER_ARP &&
            gx_chance(gx_hash(seed, id, bar - bar % 2u, 0, GX_P_PITCH), ten))
            pcs |= (uint16_t)(1u << ((h->root[bar] + 2u) % 12u));
        for (n = U->lo; n <= U->hi && nt < 24u; n++)
            if ((pcs >> (n % 12u)) & 1u)
                tones[nt++] = (int32_t)n;
        if (!nt)
            continue;
        for (step = 0; step < 16u; step++) {
            uint32_t cs = (bar & 1u) * 16u + step, idx = bar * 16u + step;
            uint32_t db = dbar(seed, id, bar, step, var);
            int32_t w = U->w[cs], note, t, vel, len;
            if (!w)
                continue;
            if (U->mode == GX_UPPER_MID && subon[idx])
                w = w * U->at_sub / 100;
            w = clampi(complex_w(w * (6400 + dd * 100) / 6400, step, dc), 0, 255);
            if (!gx_chance(gx_hash(seed, id, db, step, GX_P_ONSET), (uint32_t)w) &&
                !(U->mode == GX_UPPER_MOTIF && forced == (int32_t)cs))
                continue;
            if (U->mode == GX_UPPER_ARP) {
                uint32_t m = nt, period, q;
                switch (U->pattern) {
                case GX_ARP_UPDOWN:
                    period = m > 1u ? 2u * m - 2u : 1u;
                    q = k % period;
                    note = tones[q < m ? q : period - q];
                    break;
                case GX_ARP_CYCLE5:              /* a 5-note cycle against the 16-step bar */
                    note = tones[(k % 5u) % m];
                    if (k % 5u >= m) note += 12;
                    break;
                case GX_ARP_WALK:
                    walk += gx_chance(gx_hash(seed, id, db, step, GX_P_PITCH), 128) ? 1 : -1;
                    walk = clampi(walk, 0, (int32_t)m - 1);
                    note = tones[walk];
                    break;
                default:
                    note = tones[k % m];
                }
                k++;
            } else if (U->mode == GX_UPPER_MID) {
                static const uint8_t mw[4] = { 6, 3, 2, 0 };
                int mv = pick(gx_hash(seed, id, db, step, GX_P_PITCH), mw, 4);
                note = bass_note(U->lo, U->hi, h->root[bar], mv < 0 ? 0 : mv);
            } else {                             /* motif: a phrase walked over the chord tones, or a colour tone */
                static const int8_t mv[8] = { -7, -5, -3, -2, 2, 3, 5, 7 };
                int32_t best = tones[0], j;
                mwalk += mv[gx_range(gx_hash(seed, id, db, step, GX_P_PITCH), mlo, 7u - mlo)];
                if (mwalk > (int32_t)U->hi) mwalk -= 12;
                if (mwalk < (int32_t)U->lo) mwalk += 12;
                for (j = 1; j < (int32_t)nt; j++)  /* the chord tone nearest the walk */
                    if ((tones[j] > mwalk ? tones[j] - mwalk : mwalk - tones[j]) < (best > mwalk ? best - mwalk : mwalk - best))
                        best = tones[j];
                note = best;
                mwalk = note;
                if (gx_chance(gx_hash(seed, id, db, step, GX_P_REP), ten)) {
                    int32_t col = lowest(U->lo, (h->root[bar] + (gx_chance(gx_hash(seed, id, db, step, GX_P_LEN), 128) ? 1u : 8u)) % 12u);
                    while (col + 12 <= note) col += 12;
                    note = col <= (int32_t)U->hi ? col : note;
                }
            }
            /* Complex: an arp or motif note jumps an octave (inside the register) */
            if (leap && U->mode != GX_UPPER_MID && gx_chance(gx_hash(seed, id, db, step + 32u, GX_P_PITCH), leap)) {
                if (note + 12 <= (int32_t)U->hi) note += 12;
                else if (note - 12 >= (int32_t)U->lo) note -= 12;
            }
            vel = (int32_t)U->vel_lo + ((int32_t)U->vel_hi - (int32_t)U->vel_lo) * U->acc[step] / 9;
            vel += (int32_t)gx_range(gx_hash(seed, id, db, step, GX_P_VEL), 0, 8) - 4;
            t = clampi((int32_t)(idx * GX_STEP) + swing_units(r, p, step), 0, (int32_t)end - 1);
            len = (int32_t)U->len * (int32_t)GX_STEP - (int32_t)GX_STEP / 8;
            if (t + len > (int32_t)end)
                len = (int32_t)end - t;
            put(o, (uint32_t)t, (uint32_t)len, GX_ROLE_UPPER, 0, note, vel, 0);
        }
    }
}
