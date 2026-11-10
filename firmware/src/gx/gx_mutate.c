/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Performance operations (M4): compose, lock, reseed, mutate, similarity, undo history.
 *
 * Mutations are post-render operators on one role's events, applied in the order they were added, each
 * from its own seed. They keep a part recognisable: anchors (backbeats, downbeats, bass downbeats) are
 * immune below intensity 64; pitch moves stay on chord tones and inside the register. Because the render
 * order is drums -> chords -> sub -> upper and each mutation is applied before the next role renders,
 * the parts that listen to a mutated part hear the mutated version. */
#include "gx_profile.h"

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- composition operations ---- */

static void clear_mutations(gx_recipe_t *r)
{
    uint32_t k;
    r->mut_n = 0;
    for (k = 0; k < GX_MAXMUT; k++) {
        r->mut_mask[k] = r->mut_int[k] = 0;
        r->mut_seed[k] = 0;
    }
}

void gx_compose(gx_comp_t *c, uint32_t seed)
{
    int r;
    for (r = 0; r < (int)GX_NROLE; r++)
        if (!((c->lock >> r) & 1u)) {
            c->r[r].seed = gx_role_seed(seed, (gx_role_t)r);
            clear_mutations(&c->r[r]);
        }
}

void gx_reseed(gx_comp_t *c, gx_role_t role, uint32_t seed)
{
    if ((unsigned)role >= GX_NROLE || ((c->lock >> role) & 1u))
        return;
    c->r[role].seed = gx_role_seed(seed, role);
    clear_mutations(&c->r[role]);
}

void gx_lock(gx_comp_t *c, gx_role_t role, int on)
{
    if ((unsigned)role >= GX_NROLE)
        return;
    if (!on) {
        c->lock &= (uint8_t)~(1u << role);
        return;
    }
    if ((c->lock >> role) & 1u)
        return;                                /* already locked: keep its snapshot */
    if (role == GX_ROLE_SUB) {
        c->lsub[0] = c->r[GX_ROLE_DRUMS];
        c->lsub[1] = c->r[GX_ROLE_CHORDS];
    } else if (role == GX_ROLE_UPPER) {
        int sub_locked = (int)((c->lock >> GX_ROLE_SUB) & 1u);
        c->lup[0] = sub_locked ? c->lsub[0] : c->r[GX_ROLE_DRUMS];
        c->lup[1] = sub_locked ? c->lsub[1] : c->r[GX_ROLE_CHORDS];
        c->lup[2] = c->r[GX_ROLE_CHORDS];
        c->lup[3] = c->r[GX_ROLE_SUB];
    }
    c->lock |= (uint8_t)(1u << role);
}

int gx_mutate(gx_comp_t *c, gx_role_t role, uint8_t mask, uint8_t intensity, uint32_t seed)
{
    gx_recipe_t *r;
    if ((unsigned)role >= GX_NROLE || ((c->lock >> role) & 1u) || !(mask & GX_MUT_ALL) || !intensity)
        return -1;
    r = &c->r[role];
    if (r->mut_n >= GX_MAXMUT)
        return -1;
    r->mut_mask[r->mut_n] = (uint8_t)(mask & GX_MUT_ALL);
    r->mut_int[r->mut_n] = (uint8_t)(intensity > 127 ? 127 : intensity);
    r->mut_seed[r->mut_n] = seed ? seed : 0x9e3779b9u;
    r->mut_n++;
    return 0;
}

/* ---- the operators ---- */

static uint32_t step_of(const gx_event_t *e) { return (e->t + GX_STEP / 2u) / GX_STEP; }

/* does role/lane already play at this 16th (events [a, b))? */
static int occupied(const gx_render_t *o, uint32_t a, uint32_t b, uint32_t skip, uint8_t lane, uint32_t st)
{
    uint32_t i;
    for (i = a; i < b; i++)
        if (i != skip && o->ev[i].vel && o->ev[i].lane == lane && step_of(&o->ev[i]) == st)
            return 1;
    return 0;
}

/* the nearest chord tone above (dir > 0) or below, inside [lo, hi]; the note itself if none */
static int32_t neighbour(int32_t note, int dir, uint16_t pcs, uint32_t lo, uint32_t hi)
{
    int32_t n;
    for (n = note + (dir > 0 ? 1 : -1); n >= (int32_t)lo && n <= (int32_t)hi; n += dir > 0 ? 1 : -1)
        if ((pcs >> ((uint32_t)n % 12u)) & 1u)
            return n;
    return note;
}

void gx_mutate_apply(const gx_recipe_t *r, gx_role_t role, const gx_comp_t *c, const gx_genre_prof_t *p,
                     const gx_harmony_t *h, gx_render_t *o, uint32_t start)
{
    const uint32_t end = (uint32_t)c->bars * GX_BAR;
    uint32_t lo = 0, hi = 127, m;
    if (role == GX_ROLE_SUB) { lo = p->sub.lo; hi = p->sub.hi; }
    else if (role == GX_ROLE_CHORDS) { lo = p->chords.lo; hi = p->chords.hi; }
    else if (role == GX_ROLE_UPPER) { lo = p->upper.lo; hi = p->upper.hi; }

    for (m = 0; m < r->mut_n && m < GX_MAXMUT; m++) {
        const uint32_t seed = r->mut_seed[m], inten = r->mut_int[m], mask = r->mut_mask[m];
        uint32_t n = o->n - start, ops, j, i, k;
        uint8_t aspects[4];
        uint32_t na = 0;
        if (!n || !inten)
            continue;
        for (k = 0; k < 4u; k++)
            if ((mask >> k) & 1u)
                aspects[na++] = (uint8_t)(1u << k);
        if (!na)
            continue;
        ops = (n * inten + 254u) / 255u;       /* 1 op at the lowest intensity, about half the notes at 127 */
        if (ops > n)
            ops = n;
        for (j = 0; j < ops; j++) {
            uint32_t ei = start + gx_range(gx_hash(seed, role, j, 0, GX_P_PICK), 0, n - 1u);
            gx_event_t *e = &o->ev[ei];
            uint32_t asp = aspects[gx_range(gx_hash(seed, role, j, 1, GX_P_PICK), 0, na - 1u)];
            uint32_t h1 = gx_hash(seed, role, j, 2, GX_P_PICK);
            int dir = (h1 & 1u) ? 1 : -1;
            if (!e->vel)
                continue;                      /* already removed */
            if ((e->flags & GX_EF_ANCHOR) && inten < 64u)
                continue;                      /* the structure holds at low intensity */
            if (asp == GX_MUT_RHYTHM) {
                uint32_t st = step_of(e);
                int32_t nst = (int32_t)st + dir;
                if (inten >= 80u && role == GX_ROLE_DRUMS && !(e->flags & GX_EF_ANCHOR) &&
                    gx_chance(gx_hash(seed, role, j, 3, GX_P_PICK), 85)) {
                    e->vel = 0;                /* high intensity: drop the hit */
                    continue;
                }
                if (nst < 0 || (uint32_t)nst >= (uint32_t)c->bars * 16u)
                    continue;
                /* a pitched note never crosses a bar line into a different chord (it would no longer fit it) */
                if (role != GX_ROLE_DRUMS && h && st / 16u != (uint32_t)nst / 16u && st / 16u < GX_MAXBARS &&
                    (uint32_t)nst / 16u < GX_MAXBARS && h->pcs[st / 16u] != h->pcs[(uint32_t)nst / 16u])
                    continue;
                if (role == GX_ROLE_CHORDS) {  /* a chord moves as one */
                    uint32_t t0 = e->t;
                    for (i = start; i < o->n; i++)
                        if (o->ev[i].vel && o->ev[i].t == t0)
                            o->ev[i].t = (uint32_t)clampi((int32_t)t0 + dir * (int32_t)GX_STEP, 0, (int32_t)end - 1);
                } else {
                    if (occupied(o, start, o->n, ei, e->lane, (uint32_t)nst))
                        continue;
                    e->t = (uint32_t)clampi((int32_t)e->t + dir * (int32_t)GX_STEP, 0, (int32_t)end - 1);
                    if (e->t + e->len > end)
                        e->len = end - e->t;
                }
            } else if (asp == GX_MUT_PITCH) {
                uint32_t bar = e->t / GX_BAR;
                int32_t nn;
                if (role == GX_ROLE_DRUMS || !h || bar >= GX_MAXBARS)
                    continue;
                nn = neighbour(e->note, dir, h->pcs[bar], lo, hi);
                if (role == GX_ROLE_CHORDS)    /* no doubled note inside a chord */
                    for (i = start; i < o->n; i++)
                        if (i != ei && o->ev[i].vel && o->ev[i].t == e->t && o->ev[i].note == nn)
                            nn = e->note;
                e->note = (uint8_t)nn;
            } else if (asp == GX_MUT_VEL) {
                int32_t span = 4 + (int32_t)inten / 4;
                e->vel = (uint8_t)clampi((int32_t)e->vel + (int32_t)gx_range(h1, 0, (uint32_t)(2 * span)) - span, 1, 127);
            } else {                           /* length */
                uint32_t pct = gx_range(h1, 50, 150);
                uint32_t nl = e->len * pct / 100u;
                if (nl < GX_STEP / 4u) nl = GX_STEP / 4u;
                if (e->t + nl > end) nl = end - e->t;
                e->len = nl;
            }
        }
        /* compact: drop removed events */
        for (i = start, k = start; i < o->n; i++)
            if (o->ev[i].vel)
                o->ev[k++] = o->ev[i];
        o->n = k;
    }
    /* the sub and the upper part are monophonic (Mono sequencers on the FM-1): a note that a mutation
     * moved or lengthened into the next one ends where the next one starts */
    if ((role == GX_ROLE_SUB || role == GX_ROLE_UPPER) && r->mut_n) {
        uint32_t i, j;
        for (i = start; i < o->n; i++) {
            uint32_t next = 0xffffffffu;
            for (j = start; j < o->n; j++)
                if (j != i && o->ev[j].t > o->ev[i].t && o->ev[j].t < next)
                    next = o->ev[j].t;
            if (next != 0xffffffffu && o->ev[i].t + o->ev[i].len > next)
                o->ev[i].len = next - o->ev[i].t;
        }
    }
}

/* ---- undo history ---- */

void gx_hist_init(gx_history_t *h, const gx_comp_t *c)
{
    h->head = 0;
    h->n = 1;
    h->redo = 0;
    h->c[0] = *c;
}

void gx_hist_push(gx_history_t *h, const gx_comp_t *c)
{
    h->head = (h->head + 1u) % GX_HIST;
    h->c[h->head] = *c;
    if (h->n < GX_HIST)
        h->n++;
    h->redo = 0;
}

int gx_hist_undo(gx_history_t *h, gx_comp_t *c)
{
    if (h->n <= 1)
        return 0;
    h->head = (h->head + GX_HIST - 1u) % GX_HIST;
    h->n--;
    h->redo++;
    *c = h->c[h->head];
    return 1;
}

int gx_hist_redo(gx_history_t *h, gx_comp_t *c)
{
    if (!h->redo)
        return 0;
    h->head = (h->head + 1u) % GX_HIST;
    h->n++;
    h->redo--;
    *c = h->c[h->head];
    return 1;
}
