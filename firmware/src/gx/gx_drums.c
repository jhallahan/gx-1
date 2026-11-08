/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The drum generator: a genre profile + a recipe → drum hits for a phrase.
 *
 * Per bar, per lane, per step:
 *   weight  = the profile's 2-bar-cycle weight, shaped by Density (how much) and Syncopation (where)
 *   onset   = anchor, or a hashed chance of the weight
 *   Space   = shared silence windows that remove non-anchor hits (where silence lives)
 *   fills   = the last N steps of a fill bar use the fill weights (or mute the lane), maybe ratcheted
 * then exclusions (kick B never with kick A, a closed hat choked by the open hat) and a per-bar cap,
 * then velocity (accent contour + Humanise) and time (swing from Groove, per-lane microtiming, jitter).
 * Live lanes keep their non-anchor steps as chances (cond) that the FM-1 rolls every time round;
 * A:B lanes play only in cycle A of every B.
 *
 * Repetition: decisions are hashed on the 2-bar cycle's bar, so the groove loops. Evolution re-rolls
 * a share of decisions in later bars (hashed on the real bar), so a phrase varies without wandering. */
#include "gx_profile.h"

#define LANE_ID(l) ((uint32_t)GX_ROLE_DRUMS * 16u + (uint32_t)(l))
#define SPACE_ID ((uint32_t)GX_ROLE_DRUMS * 16u + 15u)

static int32_t mdelta(const gx_recipe_t *r, gx_macro_t m)
{
    return (int32_t)r->macro[m] - 64;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* a signed value in -a..a from a hash */
static int32_t jitter(uint32_t h, int32_t a)
{
    if (a <= 0)
        return 0;
    return (int32_t)gx_range(h, 0, (uint32_t)(2 * a)) - a;
}

/* metric position: 0 a beat, 1 an offbeat 8th, 2 a 16th between */
static int metric(uint32_t step)
{
    if ((step & 3u) == 0)
        return 0;
    if ((step & 1u) == 0)
        return 1;
    return 2;
}

/* the bar a decision is hashed on: the cycle's bar, unless Evolution re-rolls it here */
static uint32_t dbar(uint32_t seed, uint32_t id, uint32_t bar, uint32_t step, uint32_t var)
{
    if (bar < 2u)
        return bar;
    if (gx_chance(gx_hash(seed, id, bar, step, GX_P_VAR), var))
        return bar;
    return bar & 1u;
}

static uint32_t shape_weight(uint32_t w, uint32_t step, const gx_lane_prof_t *L, int32_t dd, int32_t ds)
{
    int32_t v = (int32_t)w, k;
    if (v == 0)
        return 0;                              /* macros never create hits where the genre has none */
    v = v * (6400 + dd * (int32_t)L->dens) / 6400;
    k = ds * (int32_t)L->sync / 100;           /* -128..126 */
    if (metric(step) != 0)
        v = v * (64 + k) / 64;
    else
        v = v * (64 - k) / 64;
    return (uint32_t)clampi(v, 0, 255);
}

void gx_drums_render(const gx_recipe_t *r, const gx_comp_t *c, const gx_genre_prof_t *p,
                     const gx_outmap_t *map, gx_render_t *o)
{
    const uint32_t seed = r->seed;
    const int32_t dd = mdelta(r, GX_M_DENSITY), ds = mdelta(r, GX_M_SYNC), dsp = mdelta(r, GX_M_SPACE);
    /* the part playing (M5): which lanes play, a density offset and ramp, a build, a silent last bar */
    const gx_part_prof_t *pt = &p->part[c->part < GX_NPART ? c->part : GX_PART_MAIN];
    static const uint16_t LANES_OK[5] = { 0x3ffu, 0x3ffu & ~0x20au, 0x3fcu, 0x3f0u, 0u };
    static const uint16_t ANCHOR_ONLY[5] = { 0u, 0x005u, 0u, 0u, 0u };   /* thin: kick A and snare anchors only */
    const uint16_t lane_ok = LANES_OK[pt->drums < 5u ? pt->drums : 0u];
    const uint16_t aonly = ANCHOR_ONLY[pt->drums < 5u ? pt->drums : 0u];
    const uint32_t s0 = o->n;
    const int32_t dg = mdelta(r, GX_M_GROOVE), dh = mdelta(r, GX_M_HUMAN), dev = mdelta(r, GX_M_EVOLVE);
    const uint32_t var = (uint32_t)clampi((int32_t)p->var * (64 + dev) / 64, 0, 255);
    const int32_t swing = clampi((int32_t)p->swing + dg * (int32_t)p->swing_range / 64, 50, 75);
    const int32_t vj = (int32_t)p->vjit * (64 + dh) / 64;
    const int32_t tj = (int32_t)p->tjit * (64 + dh) / 64;
    const uint32_t space_p = (uint32_t)clampi((int32_t)p->space_base + 2 * dsp, 0, 255);
    const uint32_t end = (uint32_t)c->bars * GX_BAR;
    uint32_t bar;

    for (bar = 0; bar < c->bars; bar++) {
        uint8_t hit[GX_NLANE][16];
        uint8_t wt[GX_NLANE][16];
        uint32_t cb = bar & 1u, step, l;
        int fill = p->fill_every && (bar % p->fill_every) == (uint32_t)p->fill_every - 1u;
        uint32_t fill_from = 16u - p->fill_steps;
        uint32_t ws = 0, we = 0;               /* the Space window, [ws, we) */
        const int32_t ddb = clampi(dd + pt->drum_density +
                                   (c->bars > 1u ? (int32_t)pt->ramp * (int32_t)bar / (int32_t)(c->bars - 1u) : 0), -64, 63);
        if (pt->silent_last && c->bars > 1u && bar == c->bars - 1u)
            continue;                          /* the part's last bar drops out */

        /* a shared silence window for this bar */
        {
            uint32_t db = dbar(seed, SPACE_ID, bar, 16, var);
            uint32_t h = gx_hash(seed, SPACE_ID, db, 0, GX_P_SPACE);
            if (space_p && gx_chance(h, space_p)) {
                uint32_t len = 2u + gx_range(gx_hash(seed, SPACE_ID, db, 2, GX_P_SPACE), 0,
                                             2u + (uint32_t)r->macro[GX_M_SPACE] / 16u);
                ws = gx_range(gx_hash(seed, SPACE_ID, db, 1, GX_P_SPACE), 0, 15);
                we = ws + len;
            }
        }

        /* onsets */
        for (l = 0; l < GX_NLANE; l++) {
            const gx_lane_prof_t *L = &p->lane[l];
            int ab_off = L->ab_b && (bar / 2u) % L->ab_b != (uint32_t)L->ab_a - 1u;
            int lane_on = (int)((lane_ok >> l) & 1u), anchors_only = (int)((aonly >> l) & 1u);
            for (step = 0; step < 16u; step++) {
                uint32_t cs = cb * 16u + step, w = L->w[cs], db;
                int anchor = (int)((L->anchor >> cs) & 1u);
                int in_fill = fill && step >= fill_from;
                hit[l][step] = 0;
                if (!lane_on) {
                    wt[l][step] = 0;
                    continue;
                }
                if (in_fill && L->fill_mute) {
                    wt[l][step] = 0;
                    continue;
                }
                if (in_fill && L->fill_set)
                    w = L->fill_w[step - fill_from];
                w = shape_weight(w, step, L, ddb, ds);
                wt[l][step] = (uint8_t)w;
                if (ab_off)
                    continue;
                db = dbar(seed, LANE_ID(l), bar, step, var);
                if (anchor || (!anchors_only && ((L->live && w) ||
                               gx_chance(gx_hash(seed, LANE_ID(l), db, step, GX_P_ONSET), w)))) {
                    hit[l][step] = 1;
                    if (!anchor && step >= ws && step < we &&
                        gx_chance(gx_hash(seed, LANE_ID(l), db, step, GX_P_SPACE), L->space))
                        hit[l][step] = 0;
                }
                if (anchor && hit[l][step])
                    hit[l][step] = 2;          /* 2 = an anchor: immune to exclusion and the cap */
            }
        }

        /* exclusions: a lane gives way on any step where an excluding lane plays */
        for (l = 0; l < GX_NLANE; l++) {
            const gx_lane_prof_t *L = &p->lane[l];
            uint32_t m;
            if (!L->exclude)
                continue;
            for (step = 0; step < 16u; step++) {
                if (hit[l][step] != 1)
                    continue;
                for (m = 0; m < GX_NLANE; m++)
                    if (((L->exclude >> m) & 1u) && hit[m][step]) {
                        hit[l][step] = 0;
                        break;
                    }
            }
        }

        /* the per-bar cap: drop the weakest non-anchor hits (ties broken by hash) */
        for (l = 0; l < GX_NLANE; l++) {
            const gx_lane_prof_t *L = &p->lane[l];
            uint32_t n = 0;
            if (!L->max)
                continue;
            for (step = 0; step < 16u; step++)
                n += hit[l][step] ? 1u : 0u;
            while (n > L->max) {
                uint32_t best = 16, best_key = 0xffffffffu;
                for (step = 0; step < 16u; step++) {
                    uint32_t key;
                    if (hit[l][step] != 1)
                        continue;
                    key = ((uint32_t)wt[l][step] << 24) |
                          (gx_hash(seed, LANE_ID(l), bar, step, GX_P_PICK) >> 8);
                    if (key < best_key) {
                        best_key = key;
                        best = step;
                    }
                }
                if (best == 16)
                    break;                     /* only anchors left */
                hit[l][best] = 0;
                n--;
            }
        }

        /* emit */
        for (l = 0; l < GX_NLANE; l++) {
            const gx_lane_prof_t *L = &p->lane[l];
            for (step = 0; step < 16u; step++) {
                uint32_t db = dbar(seed, LANE_ID(l), bar, step, var);
                int32_t t, vel, level;
                uint8_t flags = 0, rep = 1;
                gx_event_t *e;
                if (!hit[l][step])
                    continue;
                level = L->acc[step];
                vel = (int32_t)L->vel_lo + ((int32_t)L->vel_hi - (int32_t)L->vel_lo) * level / 9;
                vel += jitter(gx_hash(seed, LANE_ID(l), db, step, GX_P_VEL), vj);
                if (L->ghost)
                    flags |= GX_EF_GHOST;
                if (level >= 8)
                    flags |= GX_EF_ACCENT;

                t = (int32_t)(bar * GX_BAR + step * GX_STEP);
                if (step & 1u)
                    t += (swing * 2 - 100) * (int32_t)GX_STEP / 100;
                t += (int32_t)L->micro * (int32_t)GX_STEP / 100;
                t += jitter(gx_hash(seed, LANE_ID(l), db, step, GX_P_TIME), tj) * (int32_t)GX_STEP / 100;
                t = clampi(t, 0, (int32_t)end - 1);

                if (fill && p->break_edits && step >= fill_from && L->rep_p &&
                    gx_chance(gx_hash(seed, LANE_ID(l), bar, step, GX_P_REP), L->rep_p))
                    rep = (uint8_t)gx_range(gx_hash(seed, LANE_ID(l), bar, step, GX_P_LEN), 2, L->rep_max);

                if (o->n >= o->cap) {
                    o->dropped++;
                    continue;
                }
                e = &o->ev[o->n++];
                e->t = (uint32_t)t;
                e->len = (uint32_t)L->len * GX_STEP;
                e->role = GX_ROLE_DRUMS;
                e->lane = (uint8_t)l;
                e->note = (uint8_t)(map->lane_note[l] & 0x7f);
                e->vel = (uint8_t)clampi(vel, 1, 127);
                e->flags = (uint8_t)(flags | (hit[l][step] == 2 ? GX_EF_ANCHOR : 0u));
                e->rep = rep;
                /* live: a non-anchor step keeps its weight as a chance, rolled on every pass */
                e->cond = (uint8_t)(L->live && hit[l][step] == 1
                                    ? clampi((int32_t)wt[l][step] * 100 / 255, 1, 99) : 0);
            }
        }
    }

    /* a build (M5): a snare roll that tightens from 8ths to 16ths (to 32nds) with a crescendo */
    if (pt->build) {
        uint32_t step;
        for (bar = 0; bar < c->bars; bar++) {
            uint32_t pr = c->bars > 1u ? bar * 100u / (c->bars - 1u) : 100u, every = pr < 50u ? 2u : 1u;
            uint8_t rep = (uint8_t)((pt->build >= 2u && pr >= 85u) ? 2u : 1u);
            if (pt->silent_last && c->bars > 1u && bar == c->bars - 1u)
                continue;
            for (step = 0; step < 16u; step += every) {
                uint32_t i, here = bar * 16u + step, taken = 0;
                int32_t t, vel = 40 + (int32_t)(70u * here / ((uint32_t)c->bars * 16u));
                gx_event_t *e;
                for (i = s0; i < o->n; i++)
                    if (o->ev[i].lane == GX_LANE_SNARE && (o->ev[i].t + GX_STEP / 2u) / GX_STEP == here)
                        taken = 1;
                if (taken || o->n >= o->cap)
                    continue;
                t = (int32_t)(here * GX_STEP);
                if (step & 1u)
                    t += (swing * 2 - 100) * (int32_t)GX_STEP / 100;
                e = &o->ev[o->n++];
                e->t = (uint32_t)clampi(t, 0, (int32_t)end - 1);
                e->len = GX_STEP;
                e->role = GX_ROLE_DRUMS;
                e->lane = GX_LANE_SNARE;
                e->note = (uint8_t)(map->lane_note[GX_LANE_SNARE] & 0x7f);
                e->vel = (uint8_t)clampi(vel, 1, 127);
                e->flags = 0;
                e->rep = rep;
                e->cond = 0;
            }
        }
    }
}
