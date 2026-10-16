/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: playing the steps. Part of seq4.c.
 * A track's clock decides the next step (its direction, range, loop, skips) and its length (the division,
 * random divisions, the division's modulation) as the step before begins; half a step before it, the
 * step's notes go into the queue: the step's condition and chance, random perform, the modulations, the
 * repeats, the offset and swing, ties and legato (notes held into the next step, carried on if it plays
 * them again, else ended as it begins). Then each mode's own: Mono and Multitrack a note a step, Poly up
 * to seven, Chord a chord (voiced, spread, strummed, or fed to its arpeggiator), Stochastic its chances,
 * Matriceal its lanes. Also: patterns changing at their boundary, mutes, the arranger, snapshots, tap. */
#include "grids_map.h"

#define LEG_U 12u                              /* legato: the old note ends this long after the new begins */

/* ------------------------------------------------------------------------------ the ranges --- */
static void trk_range(int s, int t, int *lo, int *hi)
{
    s4_seq_t *q = &s4[s];
    int a, b, on;
    if (q->mode == M_MULTI) {
        s4_trk_t *k = &q->d.multi.t[t & 7];
        on = k->loop_on;
        a = on ? k->loop_a : k->init;
        b = on ? k->loop_b : k->end;
    } else {
        on = q->loop_on;
        a = on ? q->loop_a : q->init;
        b = on ? q->loop_b : q->end;
    }
    if (a > b) {
        int x = a;
        a = b;
        b = x;
    }
    *lo = clampi(a, 0, 127);
    *hi = clampi(b, 0, 127);
}
int s4_seq_len(int s, int trk)
{
    int lo, hi;
    trk_range(s, trk, &lo, &hi);
    return hi - lo + 1;
}
static int trk_dir(int s, int t) { return s4[s].mode == M_MULTI ? s4[s].d.multi.t[t & 7].dir : s4[s].dir; }
static int trk_div(int s, int t)
{
    int dv = s4[s].mode == M_MULTI ? s4[s].d.multi.t[t & 7].div : s4[s].div;
    if (s4[s].mode != M_MULTI && s4[s].loop_on && s4[s].loop_rate)
        dv = s4[s].loop_rate - 1;
    return clampi(dv + dm[s][t & 7][S4_D_DIV], 0, DV_N - 1);
}
static int trk_rdiv(int s, int t) { return s4[s].mode == M_MULTI ? s4[s].d.multi.t[t & 7].rdiv : s4[s].rdiv; }

static int step_skipped(int s, int t, int pos)
{
    uint8_t rtg;
    switch (s4[s].mode) {
    case M_MONO: rtg = s4[s].d.mono[pos].rtg; break;
    case M_MULTI: rtg = s4[s].d.multi.s[t & 7][pos].rtg; break;
    default: return 0;
    }
    if (!(rtg & S4_SKIP))
        return 0;
    {
        int r = s4[s].mode == M_MULTI ? s4[s].d.multi.t[t & 7].rp_rtg : s4[s].rp_rtg;
        if (r > 0 && r <= 32 && chance(r * 3))  /* random perform: a skipped step plays now and then */
            return 0;
        if (r > 32 && chance(70))
            return 0;
    }
    return 1;
}
static int rand_skip(int s, int t)             /* random perform, negative retriggers: skips */
{
    int r = s4[s].mode == M_MULTI ? s4[s].d.multi.t[t & 7].rp_rtg : s4[s].rp_rtg;
    if (s4[s].mode == M_MULTI && !s4[s].d.multi.t[t & 7].rnd_on)
        return 0;
    return r < 0 && chance(-r);
}

static int next_pos(int s, int t, clk_t *c)
{
    int lo, hi, n, tries, p = c->pos;
    trk_range(s, t, &lo, &hi);
    n = hi - lo + 1;
    for (tries = 0; tries <= n; tries++) {
        int d = trk_dir(s, t);
        if (p < lo || p > hi) {                /* outside the range (it changed): from its start */
            p = d == D_BACK ? hi : lo;
            c->dirv = (int8_t)(d == D_BACK ? -1 : 1);
        } else {
            switch (d) {
            case D_BACK:
                if (--p < lo) {
                    p = hi;
                    c->loops++;
                }
                break;
            case D_PEND:                       /* the ends twice: 1 2 3 4 4 3 2 1 1 2 .. */
            case D_PONG:                       /* 1 2 3 4 3 2 1 2 .. */
                if (n == 1)
                    break;
                if (c->dirv >= 0) {
                    if (p >= hi) {
                        c->dirv = -1;
                        if (d == D_PEND && !c->pend_rep) {
                            c->pend_rep = 1;
                            break;
                        }
                        c->pend_rep = 0;
                        p--;
                    } else
                        p++;
                } else {
                    if (p <= lo) {
                        c->dirv = 1;
                        c->loops++;
                        if (d == D_PEND && !c->pend_rep) {
                            c->pend_rep = 1;
                            break;
                        }
                        c->pend_rep = 0;
                        p++;
                    } else
                        p--;
                }
                break;
            case D_RAND:
                p = lo + rnd_n(n);
                if (++c->count % (uint16_t)n == 0)
                    c->loops++;
                break;
            case D_DRUNK: {                    /* half the time on, a quarter the same, a quarter back */
                int r = rnd_n(4);
                p += r < 2 ? 1 : r == 2 ? 0 : -1;
                if (p > hi) {
                    p = lo;
                    c->loops++;
                }
                if (p < lo)
                    p = hi;
                break;
            }
            default:
                if (++p > hi) {
                    p = lo;
                    c->loops++;
                }
                break;
            }
        }
        if (!step_skipped(s, t, p) && !rand_skip(s, t))
            break;
    }
    return p;
}
static uint32_t step_units(int s, int t)
{
    uint32_t u = s4_div_units(trk_div(s, t));
    int rd = trk_rdiv(s, t);
    if (rd && chance(rd))                      /* random division: twice or half as long */
        u = (s4_rand() & 1u) ? u * 2u : u / 2u;
    return u ? u : 960u;
}

static void clk_start(int s, int t, clk_t *c, uint32_t at)
{
    int lo, hi;
    trk_range(s, t, &lo, &hi);
    c->started = 0;
    c->prep = 0;
    c->loops = 0;
    c->count = 0;
    c->prev = 0;
    c->pend_rep = 0;
    c->dirv = (int8_t)(trk_dir(s, t) == D_BACK ? -1 : 1);
    c->npos = (uint8_t)(trk_dir(s, t) == D_BACK ? hi : trk_dir(s, t) == D_RAND ? lo + rnd_n(hi - lo + 1) : lo);
    c->pos = c->npos;
    c->nt = c->t = at;
    c->nlen = c->len = step_units(s, t);
    rt[s].h[t & 7].n = 0;
}

/* ------------------------------------------------------------------------ notes of a step --- */
static held_t hprev;                           /* the track's held notes as the step began */
static void step_begin(int s, int t)
{
    hprev = rt[s].h[t];
    rt[s].h[t].n = 0;
}
static uint8_t step_cont(int note)             /* a held note this step plays again: carried on */
{
    int k;
    for (k = 0; k < hprev.n; k++)
        if (hprev.note[k] == note && hprev.tok[k]) {
            uint8_t tk = hprev.tok[k];
            hprev.tok[k] = 0;
            return tk;
        }
    return 0;
}
static void step_end(int s, int t, uint32_t T) /* the held notes not carried on end as the step begins */
{
    int k;
    for (k = 0; k < hprev.n; k++)
        if (hprev.tok[k])
            q_add(s, hprev.leg ? T + LEG_U : T, E_OFF, hprev.port, hprev.ch, hprev.note[k], 0, hprev.tok[k], t);
    hprev.n = 0;
}

/* one note: its repeats in the step, its gate, held on if tied */
static void note_sched(int s, int t, int port, int ch, int note, int vel, uint32_t ton, uint32_t sub, int gate, int rep, int cont)
{
    int r;
    if (note < 0 || note > 127)
        return;
    for (r = 0; r < rep; r++) {
        uint32_t at = ton + (uint32_t)r * sub;
        uint8_t tk = 0;
        if (r == 0 && cont && rep == 1)
            tk = step_cont(note);
        if (!tk) {
            tk = tok_new();
            q_add(s, at, E_ON, port, ch, note, vel, tk, t);
        }
        if (r == rep - 1 && gate >= S4_TIE) {
            held_t *h = &rt[s].h[t];
            if (h->n < 8) {
                h->note[h->n] = (uint8_t)note;
                h->tok[h->n] = tk;
                h->n++;
                h->port = (uint8_t)port;
                h->ch = (uint8_t)ch;
                h->leg = (uint8_t)(gate == S4_LEG);
                continue;
            }
        }
        {
            uint32_t dur = sub * (uint32_t)(gate > 100 ? 100 : gate) / 100u;
            if (dur < 8u)
                dur = 8u;
            if (r < rep - 1 && dur >= sub)
                dur = sub - 2u;                /* (repeats: each its own note) */
            q_add(s, at + dur, E_OFF, port, ch, note, 0, tk, t);
        }
    }
}

static void ab_decode(int idx, int *a, int *b)
{
    int B, A;
    for (B = 2; B <= 8; B++)
        for (A = 1; A <= B; A++)
            if (idx-- == 0) {
                *a = A;
                *b = B;
                return;
            }
    *a = 1;
    *b = 2;
}
static int cond_ok(int s, int t, clk_t *c, int cond, int locked)
{
    int a, b;
    if (cond <= 100) {
        if (locked)
            return 1;
        return chance(clampi(cond + dm[s][t & 7][S4_D_PROB], 0, 100));
    }
    if (cond < S4_C_NAB) {
        ab_decode(cond - S4_C_AB, &a, &b);
        return c->loops % b == (uint32_t)(a - 1);
    }
    if (cond < S4_C_PRE) {
        ab_decode(cond - S4_C_NAB, &a, &b);
        return c->loops % b != (uint32_t)(a - 1);
    }
    switch (cond) {
    case S4_C_PRE: return c->prev;
    case S4_C_NPRE: return !c->prev;
    case S4_C_FIRST: return c->loops == 0;
    case S4_C_NFIRST: return c->loops > 0;
    case S4_C_FILL: return fill_on;
    case S4_C_NFILL: return !fill_on;
    default: return 1;                         /* FIXED, LOCKED */
    }
}

/* random perform on a note (locked steps are left alone) */
typedef struct {
    int vel, oct, gate, rep, trig;
} rp_t;
static void rand_perform(int s, int t, rp_t *o)
{
    s4_seq_t *q = &s4[s];
    int rv, ro, rg = 0, rtg, rtr;
    o->vel = o->oct = o->gate = o->rep = 0;
    o->trig = 1;
    if (q->mode == M_MULTI) {
        s4_trk_t *k = &q->d.multi.t[t & 7];
        if (!k->rnd_on)
            return;
        rv = k->rp_vel;
        ro = 0;
        rg = k->rp_gate;
        rtr = k->rp_trig;
        rtg = k->rp_rtg;
    } else {
        rv = q->rp_vel;
        ro = q->rp_oct;
        rtr = q->rp_trig;
        rtg = q->rp_rtg;
    }
    if (rv > 0)
        o->vel = rnd_n(rv * 127 / 100 + 1);
    else if (rv < 0)
        o->vel = rnd_n(-rv * 254 / 100 + 1) + rv * 127 / 100;
    if (ro && chance(ro < 0 ? -ro : ro)) {     /* octaves: more of them and further the higher */
        int range = 1 + (ro < 0 ? -ro : ro) / 34;
        o->oct = 1 + rnd_n(range);
        if (ro < 0 && (s4_rand() & 1u))
            o->oct = -o->oct;
    }
    if (rg > 0)
        o->gate = rnd_n(rg + 1);
    else if (rg < 0)
        o->gate = rnd_n(-rg * 2 + 1) + rg;
    if (rtr < 100 && !chance(rtr))
        o->trig = 0;
    if (rtg > 33 && chance((rtg - 33) * 3 / 2))
        o->rep = 1 + rnd_n(3);
}

static int trk_muted(int s, int t)
{
    s4_seq_t *q = &s4[s];
    if (q->mute)
        return 1;
    if (q->mode == M_MULTI) {
        int k, solo = 0;
        for (k = 0; k < S4_NTRK; k++)
            solo |= q->d.multi.t[k].solo;
        if (solo)
            return !q->d.multi.t[t & 7].solo;
        return q->d.multi.t[t & 7].mute;
    }
    return 0;
}

/* the offset (and swing) of a step's notes, in units from the step's start */
static int32_t step_ofs(int s, clk_t *c, uint32_t L, int ofs)
{
    int32_t o = (int32_t)L * clampi(ofs, -45, 45) / 100;
    if ((c->count & 1u) && s4[s].swing > 50)   /* the odd steps late: the swing */
        o += (int32_t)L * (s4[s].swing - 50) / 50;
    return o + ms_units(s4[s].nudge);
}

static void glide_send(int s, int port, int ch, int glide)
{
    rt_t *r = &rt[s];
    if (!s4[s].glide_cc)
        return;
    if (glide) {
        out3(port, 0xB0 | ch, 5, glide * 127 / 100);
        if (!r->glide_on)
            out3(port, 0xB0 | ch, 65, 127);
        r->glide_on = 1;
    } else if (r->glide_on) {
        out3(port, 0xB0 | ch, 65, 0);
        r->glide_on = 0;
    }
}

/* Mono and Multitrack: a note a step */
static int trk_note(int s, int t, int deg)
{
    int sc = s4[s].scale >= S4_HARMO ? 0 : s4[s].scale;
    if (s4[s].scale >= S4_HARMO)
        return s4_note_of(s, deg, 0) - seq_base(s) + s4[s].d.multi.t[t & 7].note;
    return s4[s].d.multi.t[t & 7].note + deg_semis(sc, deg);
}
static int euclid_hit(int len, int puls, int rot, int i)   /* the Euclidean rhythm (Bresenham) */
{
    if (len <= 0 || puls <= 0)
        return 0;
    if (puls >= len)
        return 1;
    i = ((i - rot) % len + len) % len;
    return (i * puls) % len < puls;
}
static void play_note_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    s4_seq_t *q = &s4[s];
    int multi = q->mode == M_MULTI;
    s4_step_t st = multi ? q->d.multi.s[t & 7][pos] : q->d.mono[pos];
    int16_t *m = dm[s][t & 7];
    int port, ch, trig, locked, gen = 0;
    rp_t rp;
    trk_port(s, t, &port, &ch);
    step_begin(s, t);
    trig = st.vel && !(st.rtg & (S4_MUTED | S4_SKIP)) && !trk_muted(s, t);
    locked = st.cond == S4_C_FIXED || st.cond == S4_C_LOCKED;
    if (multi && !st.vel && !trk_muted(s, t)) {   /* the generators' triggers on empty steps */
        s4_trk_t *k = &q->d.multi.t[t & 7];
        if (k->dg_on && t < 3) {               /* the drum generator: Grids' map */
            int x = clampi(q->dg_x + m[S4_D_DG_X], 0, 255), y = clampi(q->dg_y + m[S4_D_DG_Y], 0, 255);
            int lvl = grids_level((uint8_t)(c->count & 31u), (uint8_t)t, (uint8_t)x, (uint8_t)y);
            int pert = rt[s].pert[t] * clampi(q->dg_chaos + m[S4_D_DG_CHAOS], 0, 255) / 1024;
            int dens = clampi(k->dg_dens + m[S4_D_DG_K + t], 0, 100) * 255 / 100;
            lvl = lvl + pert > 255 ? 255 : lvl + pert;
            if (dens && lvl > 255 - dens) {
                st.vel = (uint8_t)(lvl > 192 ? 127 : 96);
                gen = 1;
            }
        }
        if (!gen && (m[S4_D_EU_PULS] || m[S4_D_EU_ROT]) && k->eu_len) {   /* Euclid, modulated */
            int lo, hi;
            trk_range(s, t, &lo, &hi);
            if (euclid_hit(k->eu_len, clampi(k->eu_puls + m[S4_D_EU_PULS], 0, k->eu_len), k->eu_rot + m[S4_D_EU_ROT], pos - lo)) {
                st.vel = q->vdef ? q->vdef : 100;
                gen = 1;
            }
        }
        if (gen) {
            st.gate = q->gdef ? q->gdef : 50;
            st.deg = 0;
            st.oct = 0;
            st.cond = 100;
            st.rtg = 0;
            st.ofs = 0;
            trig = 1;
            s4p.gen[s][t & 7][(c->count >> 3) & 3] |= (uint8_t)(1u << (c->count & 7u));
        }
    }
    if (trig)
        trig = cond_ok(s, t, c, st.cond, locked);
    if (trig && !locked) {
        rand_perform(s, t, &rp);
        trig = rp.trig;
    } else {
        rp.vel = rp.oct = rp.gate = rp.rep = 0;
    }
    if (trig) {
        int vel = clampi(st.vel + (locked ? 0 : m[S4_D_VEL] + rp.vel), 1, 127);
        int oct = st.oct + (locked ? 0 : m[S4_D_OCT] + rp.oct);
        int deg = st.deg + (locked ? 0 : m[S4_D_PITCH]);
        int note = (multi ? trk_note(s, t, deg) + 12 * oct : s4_note_of(s, deg, oct)) + rt[s].tpose;
        int gate = st.gate >= S4_TIE ? st.gate : clampi(st.gate + (locked ? 0 : m[S4_D_GATE] + rp.gate), 1, 100);
        int rep = clampi((st.rtg & S4_RTG_N) + 1 + (locked ? 0 : m[S4_D_RTG] + rp.rep), 1, 5);
        int32_t o = step_ofs(s, c, L, st.ofs);
        uint32_t ton = (uint32_t)((int32_t)T + o);
        glide_send(s, port, ch, clampi(st.glide + m[S4_D_GLIDE], 0, 100));
        note_sched(s, t, port, ch, note, vel, ton, L / (uint32_t)rep, gate, rep, 1);
        lfo_retrig(s);
        s4p.lit[s][t & 7]++;
    }
    step_end(s, t, T);
    c->prev = (uint8_t)trig;
}

/* Poly: each of the step's notes its own */
static void play_poly_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    s4_seq_t *q = &s4[s];
    int16_t *m = dm[s][0];
    int port, ch, i, any = 0, n = 0, maxn = q->poly_max ? q->poly_max : S4_POLY;
    uint8_t notes[8], vels[8];
    trk_port(s, 0, &port, &ch);
    step_begin(s, t);
    if (!trk_muted(s, t))
        for (i = 0; i < q->d.poly.n && n < maxn; i++) {
            s4_pnote_t *pn = &q->d.poly.note[i];
            rp_t rp;
            int locked, vel, gate, rep, note;
            int32_t o;
            if (pn->step != pos || !pn->vel || (pn->rtg & S4_MUTED))
                continue;
            locked = pn->cond == S4_C_FIXED || pn->cond == S4_C_LOCKED;
            if (!cond_ok(s, t, c, pn->cond, locked))
                continue;
            if (!locked) {
                rand_perform(s, t, &rp);
                if (!rp.trig)
                    continue;
            } else
                rp.vel = rp.oct = rp.rep = 0;
            vel = clampi(pn->vel + (locked ? 0 : m[S4_D_VEL] + rp.vel), 1, 127);
            note = s4_note_of(s, pn->deg + (locked ? 0 : m[S4_D_PITCH]), pn->oct + (locked ? 0 : m[S4_D_OCT] + rp.oct)) + rt[s].tpose;
            gate = pn->gate >= S4_TIE ? pn->gate : clampi(pn->gate + (locked ? 0 : m[S4_D_GATE]), 1, 100);
            rep = clampi((pn->rtg & S4_RTG_N) + 1 + (locked ? 0 : m[S4_D_RTG] + rp.rep), 1, 5);
            o = step_ofs(s, c, L, pn->ofs);
            notes[n] = (uint8_t)clampi(note, 0, 127);
            vels[n] = (uint8_t)vel;
            n++;
            any = 1;
            if (!q->arp.mode)
                note_sched(s, t, port, ch, note, vel, (uint32_t)((int32_t)T + o), L / (uint32_t)rep, gate, rep, 1);
        }
    if (n) {                                   /* what the harmonizer follows */
        int k;
        sort_u8(notes, n);
        for (k = 0; k < n; k++)
            s4p.chord[s][k] = notes[k];
        s4p.chord_n[s] = (uint8_t)n;
        if (q->arp.mode)
            arp_feed(s, notes, vels, n, T, L);
        glide_send(s, port, ch, q->d.poly.glide[pos]);
        lfo_retrig(s);
        s4p.lit[s][0]++;
    }
    step_end(s, t, T);
    c->prev = (uint8_t)any;
}

/* Chord: a chord a step, voiced, spread, strummed (or arpeggiated) */
static void play_chord_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    s4_seq_t *q = &s4[s];
    s4_cstep_t cs = q->d.chord[pos];
    int16_t *m = dm[s][0];
    int port, ch, trig, locked, n, k;
    uint8_t notes[8], vels[8];
    rp_t rp;
    trk_port(s, 0, &port, &ch);
    step_begin(s, t);
    trig = cs.vel && !(cs.rtg & S4_MUTED) && !trk_muted(s, t);
    locked = cs.cond == S4_C_FIXED || cs.cond == S4_C_LOCKED;
    if (trig)
        trig = cond_ok(s, t, c, cs.cond, locked);
    if (trig && !locked) {
        rand_perform(s, t, &rp);
        trig = rp.trig;
    } else
        rp.vel = rp.oct = rp.rep = 0;
    if (trig) {
        int vel = clampi(cs.vel + (locked ? 0 : m[S4_D_VEL] + rp.vel), 1, 127);
        int gate = cs.gate >= S4_TIE ? cs.gate : clampi(cs.gate + (locked ? 0 : m[S4_D_GATE]), 1, 100);
        int rep = clampi((cs.rtg & S4_RTG_N) + 1 + (locked ? 0 : m[S4_D_RTG] + rp.rep), 1, 5);
        int strum = clampi(cs.strum + (locked ? 0 : m[S4_D_STRUM]), -100, 100);
        int32_t o = step_ofs(s, c, L, cs.ofs);
        if (!locked) {
            cs.deg = (int8_t)(cs.deg + m[S4_D_CH_ROOT] + m[S4_D_PITCH]);
            cs.type = (uint8_t)clampi(cs.type + m[S4_D_CH_TYPE], 0, 30);
            cs.voi = (uint8_t)clampi(cs.voi + m[S4_D_CH_VOI], 0, 5);
            cs.spread = (uint8_t)clampi(cs.spread + m[S4_D_SPREAD], 0, 7);
            cs.oct = (int8_t)(cs.oct + m[S4_D_OCT] + rp.oct);
        }
        n = s4_chord_notes(s, &cs, notes);
        if (q->ch_auto)
            voice_lead(notes, n, rt[s].prev, rt[s].pn);
        for (k = 0; k < n; k++) {
            notes[k] = (uint8_t)clampi(notes[k] + rt[s].tpose, 0, 127);
            vels[k] = (uint8_t)vel;
            rt[s].prev[k] = notes[k];
            s4p.chord[s][k] = notes[k];
        }
        rt[s].pn = (uint8_t)n;
        s4p.chord_n[s] = (uint8_t)n;
        if (q->arp.mode) {
            arp_feed(s, notes, vels, n, T, L * (uint32_t)(gate > 100 ? 100 : gate) / 100u);
        } else {
            for (k = 0; k < n; k++) {          /* the strum: low to high (or high to low), up to an eighth of the step a note */
                int idx = strum >= 0 ? k : n - 1 - k;
                int32_t so = (int32_t)L * (strum < 0 ? -strum : strum) / 800 * idx;
                note_sched(s, t, port, ch, notes[k], vel, (uint32_t)((int32_t)T + o + so), L / (uint32_t)rep, gate, rep, 1);
            }
        }
        glide_send(s, port, ch, cs.glide);
        lfo_retrig(s);
        s4p.lit[s][0]++;
    }
    step_end(s, t, T);
    c->prev = (uint8_t)trig;
}

/* Stochastic: the chances decide (or replay what they decided, as far as they are locked) */
static void play_stoch_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    s4_seq_t *q = &s4[s];
    s4_stoch_t *st = &q->d.stoch;
    s4_step_t *b = &st->buf[pos];
    int16_t *m = dm[s][0];
    int port, ch, rhythm = clampi(st->rhythm + m[S4_D_ST_RHYTHM], 0, 7), pitch = clampi(st->pitch + m[S4_D_ST_PITCH], 0, 7);
    rp_t rp;
    trk_port(s, 0, &port, &ch);
    step_begin(s, t);
    if (chance(rhythm * 100 / 7)) {            /* a new rhythm here: does it play */
        int nx = clampi(st->next, 0, 7);
        b->vel = (uint8_t)(chance(nx * 100 / 7) ? (q->vdef ? q->vdef : 100) : 0);
    }
    if (b->vel && (chance(pitch * 100 / 7) || b->gate == 0)) {   /* a new note: the weighted semitones */
        int tot = 0, k, r, pc = 0, up = clampi(st->oct_up + m[S4_D_ST_UP], 0, 3), dn = clampi(st->oct_dn + m[S4_D_ST_DN], 0, 3);
        int w[12];
        for (k = 0; k < 12; k++) {
            w[k] = clampi(st->prob[k] + m[S4_D_ST_PROB], 0, 7);
            tot += w[k];
        }
        if (tot) {
            r = rnd_n(tot);
            for (k = 0; k < 12; k++) {
                if (r < w[k]) {
                    pc = k;
                    break;
                }
                r -= w[k];
            }
            b->deg = (int8_t)pc;
            b->oct = (int8_t)((up || dn) ? rnd_n(up + dn + 1) - dn : 0);
        } else
            b->vel = 0;
        b->gate = q->gdef ? q->gdef : 75;
    }
    if (b->vel && !trk_muted(s, t)) {
        rand_perform(s, t, &rp);
        if (rp.trig) {
            int note = (q->scale >= S4_HARMO ? s4_note_of(s, b->deg, 0) : seq_base(s) + b->deg) + 12 * (b->oct + rp.oct + m[S4_D_OCT]) + rt[s].tpose;
            int vel = clampi(b->vel + m[S4_D_VEL] + rp.vel, 1, 127);
            int gate = clampi(b->gate + m[S4_D_GATE], 1, 100), rep = clampi(1 + rp.rep + m[S4_D_RTG], 1, 5);
            glide_send(s, port, ch, gate > 50 ? (gate - 50) * 2 : 0);
            note_sched(s, t, port, ch, note, vel, (uint32_t)((int32_t)T + step_ofs(s, c, L, 0)), L / (uint32_t)rep, gate, rep, 0);
            lfo_retrig(s);
            s4p.lit[s][0]++;
        }
    }
    step_end(s, t, T);
}
static uint32_t stoch_units(int s, uint32_t u)  /* Stochastic's steps: longer or shorter now and then */
{
    s4_stoch_t *st = &s4[s].d.stoch;
    int lg = clampi(st->div_long + dm[s][0][S4_D_ST_LONG], 0, 3), sh = clampi(st->div_short + dm[s][0][S4_D_ST_SHORT], 0, 3);
    if (lg && chance(lg * 15))
        return u * 2u;
    if (sh && chance(sh * 15))
        return u / 2u;
    return u;
}

static void play_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    switch (s4[s].mode) {
    case M_POLY: play_poly_step(s, t, c, pos, T, L); break;
    case M_CHORD: play_chord_step(s, t, c, pos, T, L); break;
    case M_STOCH: play_stoch_step(s, t, c, pos, T, L); break;
    default: play_note_step(s, t, c, pos, T, L); break;
    }
}

/* a track's clock: prepare the next step half a step early, begin it on time */
static void trk_run(int s, int t)
{
    rt_t *r = &rt[s];
    clk_t *c = &r->c[t];
    int guard = 8;
    while (guard--) {
        if (!c->prep && (int32_t)(r->lpos + c->nlen / 2u - c->nt) >= 0) {
            play_step(s, t, c, c->npos, c->nt, c->nlen);
            c->prep = 1;
        }
        if ((int32_t)(r->lpos - c->nt) < 0 || !c->prep)
            break;
        c->pos = c->npos;                      /* begins */
        c->t = c->nt;
        c->len = c->nlen;
        c->count++;
        c->started = 1;
        s4p.step[s][t] = c->pos;
        rec_commit(s, t, c->pos);
        c->npos = (uint8_t)next_pos(s, t, c);
        c->nlen = step_units(s, t);
        if (s4[s].mode == M_STOCH)
            c->nlen = stoch_units(s, c->nlen);
        c->nt = c->t + c->len;
        c->prep = 0;
    }
}

/* ----------------------------------------------------------------------------- Matriceal --- */
static void clk_start_lane(int s, int t, int l, clk_t *c, uint32_t at)
{
    s4_lane_t *ln = &s4[s].d.mx[t & 3].lane[l];
    c->prep = 0;
    c->loops = 0;
    c->count = 0;
    c->dirv = 1;
    c->pend_rep = 0;
    c->pulse = 0;
    c->pos = c->npos = (uint8_t)(ln->dir == D_BACK ? ln->end : ln->init);
    c->t = c->nt = at;
    c->len = c->nlen = s4_div_units(ln->div ? ln->div : DV_16);
}
static int lane_next(int s, int t, int l, clk_t *c)
{
    s4_lane_t *ln = &s4[s].d.mx[t & 3].lane[l];
    int lo = ln->init & 15, hi = ln->end & 15, p = c->pos, n;
    if (lo > hi) {
        n = lo;
        lo = hi;
        hi = n;
    }
    n = hi - lo + 1;
    switch (ln->dir) {
    case D_BACK: p = p - 1 < lo ? hi : p - 1; break;
    case D_PEND:
    case D_PONG:
        if (n == 1)
            break;
        if (c->dirv >= 0) {
            if (p >= hi) {
                c->dirv = -1;
                p--;
            } else
                p++;
        } else {
            if (p <= lo) {
                c->dirv = 1;
                p++;
            } else
                p--;
        }
        break;
    case D_RAND: p = lo + rnd_n(n); break;
    case D_DRUNK: {
        int r = rnd_n(4);
        p += r < 2 ? 1 : r == 2 ? 0 : -1;
        p = p > hi ? lo : p < lo ? hi : p;
        break;
    }
    default: p = p + 1 > hi ? lo : p + 1; break;
    }
    return p;
}
static void mx_run(int s, int t)
{
    rt_t *r = &rt[s];
    s4_mxtrk_t *x = &s4[s].d.mx[t & 3];
    int l;
    /* the lanes other than the triggers: their playheads move on their own */
    for (l = 1; l < S4_NLANE; l++) {
        clk_t *c = &r->lc[t][l];
        int guard = 8;
        while (guard-- && (int32_t)(r->lpos - c->nt) >= 0) {
            c->t = c->nt;
            c->pos = c->npos;
            c->npos = (uint8_t)lane_next(s, t, l, c);
            c->len = s4_div_units(x->lane[l].div ? x->lane[l].div : DV_16);
            c->nt = c->t + c->len;
        }
    }
    {   /* the triggers */
        clk_t *c = &r->lc[t][L_TRIG];
        int guard = 8;
        while (guard--) {
            if (!c->prep && (int32_t)(r->lpos + c->nlen / 2u - c->nt) >= 0) {
                mx_step(s, t, c, c->npos, c->nt, c->nlen);
                c->prep = 1;
            }
            if ((int32_t)(r->lpos - c->nt) < 0 || !c->prep)
                break;
            c->pos = c->npos;
            c->t = c->nt;
            c->len = c->nlen;
            s4p.step[s][t] = c->pos;
            if (c->pulse > 1) {                /* pulses: the trigger lane stays on this step */
                c->pulse--;
            } else {
                c->pulse = 0;
                c->npos = (uint8_t)lane_next(s, t, L_TRIG, c);
            }
            c->nlen = s4_div_units(x->lane[L_TRIG].div ? x->lane[L_TRIG].div : DV_16);
            c->nt = c->t + c->len;
            c->prep = 0;
        }
    }
}
static int lane_val(int s, int t, int l, int *ok)  /* a lane's value now (ok: its chance came up) */
{
    s4_mxtrk_t *x = &s4[s].d.mx[t & 3];
    int p = rt[s].lc[t & 3][l].pos & 15, pr = x->p[l][p];
    *ok = (l != L_NOTE && l != L_INTV && l != L_OCT && l != L_RTG) || pr == 0 || chance(pr);
    return x->v[l][p];
}
static void mx_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L)
{
    rt_t *r = &rt[s];
    s4_mxtrk_t *x = &s4[s].d.mx[t & 3];
    int port = x->port & 3, ch = x->ch & 15, ok, trig, k;
    int16_t *m = dm[s][t & 3];
    step_begin(s, t);
    trig = x->v[L_TRIG][pos & 15] && !s4[s].mute;
    if (c->pulse > 1) {                        /* a repeat of a pulsed step: all, the first only, or tied */
        int type = x->p[L_PULS][rt[s].lc[t][L_PULS].pos & 15];
        if (type == 1)
            trig = 0;
    }
    if (trig) {
        int prob = lane_val(s, t, L_PROB, &ok);
        if (!chance(clampi(prob ? prob : 100, 0, 100) + m[S4_D_PROB]))
            trig = 0;
    }
    if (trig) {
        int nv = lane_val(s, t, L_NOTE, &ok), note, vel, oct, gate, rep, iv, puls;
        if (!ok)
            nv = 0;
        iv = lane_val(s, t, L_INTV, &ok);
        if (ok) {                              /* the interval: summed, and back to the far end at a bound */
            s4_lane_t *ln = &x->lane[L_INTV];
            int mn = (int8_t)ln->min, mxv = (int8_t)ln->max;
            r->acc[t] = (int16_t)(r->acc[t] + iv + m[S4_D_MX_INTV]);
            if (mxv > mn) {
                if (r->acc[t] > mxv)
                    r->acc[t] = (int16_t)mn;
                if (r->acc[t] < mn)
                    r->acc[t] = (int16_t)mxv;
            }
        }
        oct = lane_val(s, t, L_OCT, &ok);
        if (!ok)
            oct = 0;
        vel = lane_val(s, t, L_VEL, &ok);
        gate = lane_val(s, t, L_GATE, &ok);
        rep = lane_val(s, t, L_RTG, &ok);
        if (!ok)
            rep = 1;
        puls = lane_val(s, t, L_PULS, &ok);
        if (puls > 1 && c->pulse == 0)
            c->pulse = (uint8_t)clampi(puls, 1, 8);
        if (c->pulse > 1 && x->p[L_PULS][rt[s].lc[t][L_PULS].pos & 15] == 2)
            gate = S4_TIE;
        note = s4_note_of(s, x->note + nv + r->acc[t] + m[S4_D_PITCH], oct + m[S4_D_OCT]) + r->tpose;
        vel = clampi((vel ? vel : 100) + m[S4_D_VEL], 1, 127);
        gate = gate >= S4_TIE ? gate : clampi((gate ? gate : 50) + m[S4_D_GATE], 5, 100);
        rep = rep < 0 ? 1 : clampi(rep + m[S4_D_RTG], 1, 5);
        k = lane_val(s, t, L_GLIDE, &ok);
        glide_send(s, port, ch, k);
        note_sched(s, t, port, ch, note, vel, T + (uint32_t)ms_units(s4[s].nudge), L / (uint32_t)rep, gate, rep, 1);
        lfo_retrig(s);
        s4p.lit[s][t & 7]++;
        if (x->rlen && ++r->mxtrig[t] >= x->rlen) {   /* the reset length: every lane from its start */
            int l;
            r->mxtrig[t] = 0;
            for (l = 1; l < S4_NLANE; l++)
                clk_start_lane(s, t, l, &r->lc[t][l], T + L);
            r->acc[t] = 0;
        }
    }
    step_end(s, t, T);
}

/* ------------------------------------------------------------------- mod lanes' own clocks --- */
static void clk_start_mod(int s, int l, clk_t *c, uint32_t at)
{
    s4_modl_t *m = &s4[s].ml[l];
    c->pos = c->npos = m->init;
    c->t = c->nt = at;
    c->len = c->nlen = s4_div_units(m->div ? m->div : s4[s].div);
}
static void lane_clock(int s, int l)
{
    rt_t *r = &rt[s];
    clk_t *c = &r->mc[l];
    s4_modl_t *m = &s4[s].ml[l];
    int guard = 8;
    while (guard-- && (int32_t)(r->lpos - c->nt) >= 0) {
        int lo = m->init, hi = m->end >= m->init ? m->end : m->init;
        c->t = c->nt;
        c->pos = c->npos;
        c->npos = (uint8_t)(c->pos + 1 > hi ? lo : c->pos + 1);
        c->len = s4_div_units(m->div ? m->div : s4[s].div);
        c->nt = c->t + c->len;
    }
}

static void seq_run(int s)
{
    int t;
    switch (s4[s].mode) {
    case M_MULTI:
        for (t = 0; t < S4_NTRK; t++)
            trk_run(s, t);
        break;
    case M_MATRIX:
        for (t = 0; t < S4_NMX; t++)
            mx_run(s, t);
        break;
    default:
        trk_run(s, 0);
        break;
    }
}

/* ---------------------------------------------------------------- patterns, mutes, songs --- */
static void pattern_swap(int s)                /* the staged pattern in, from the boundary */
{
    uint8_t keep[S4_NTRK], keepsolo[S4_NTRK];
    int t, was_multi = s4[s].mode == M_MULTI;
    for (t = 0; t < S4_NTRK; t++) {
        keep[t] = was_multi ? s4[s].d.multi.t[t].mute : 0;
        keepsolo[t] = was_multi ? s4[s].d.multi.t[t].solo : 0;
    }
    q_flush(s);
    {
        const uint32_t *a = (const uint32_t *)(const void *)&s4_stage[s];
        uint32_t *b = (uint32_t *)(void *)&s4[s], i;
        for (i = 0; i < sizeof(s4_seq_t) / 4u; i++)
            b[i] = a[i];
    }
    if (s4g.keep_mutes && was_multi && s4[s].mode == M_MULTI)
        for (t = 0; t < S4_NTRK; t++) {
            s4[s].d.multi.t[t].mute = keep[t];
            s4[s].d.multi.t[t].solo = keepsolo[t];
        }
    s4p.pat[s] = (uint8_t)(qpat[s] ? qpat[s] - 1 : s4p.pat[s]);
    qpat[s] = 0;
    s4p.next[s] = 0xFF;
    seq_reset(s, rt[s].lpos);
    send_pc(s);
}

static void mute_set(int s, int on)
{
    if (s4g.mute_mode <= 1) {                  /* toggling: the sequencer stops, or starts at its boundary */
        if (on) {
            rt[s].active = 0;
            q_flush(s);
        } else if (!rt[s].active) {
            if (playing)
                rt[s].waiting = 1;
            else
                rt[s].active = 1;
        }
        s4p.active[s] = rt[s].active;
        s4[s].mute = 0;
    } else {
        s4[s].mute = (uint8_t)on;
        if (on)
            q_flush(s);
    }
}

static void snapshot_restore(void)
{
    int s;
    for (s = 0; s < S4_NSEQ; s++) {
        const uint32_t *a = (const uint32_t *)(const void *)&s4_snapbuf[s];
        uint32_t *b = (uint32_t *)(void *)&s4[s], i;
        q_flush(s);
        for (i = 0; i < sizeof(s4_seq_t) / 4u; i++)
            b[i] = a[i];
    }
}

static void tap(void)
{
    uint32_t n = tap_n, i, sum = 0, cnt = 0, lim = s4g.taps ? s4g.taps : 8;
    if (n && now_us - tap_t[(n - 1) % 8] > 2000000u)
        n = 0;
    tap_t[n % 8] = now_us;
    n++;
    tap_n = (uint8_t)(n > 64 ? 64 : n);
    for (i = 1; i < n && cnt < lim && i < 8; i++) {
        sum += tap_t[(n - i) % 8] - tap_t[(n - i - 1) % 8];
        cnt++;
    }
    if (cnt)
        s4g.tempo = (uint16_t)clampi((int)(600000000u / (sum / cnt)), 300, 3000);
}

/* the arranger: each lane's slots, played so many times; the master lane leads */
static void song_req(int s)
{
    s4_song_t *g = &s4song[s4p.song % S4_NSONG];
    s4_slot_t *sl = &g->slot[s][s4p.slot[s] % S4_NSLOT];
    if (sl->pat == S4_EMPTY)
        return;
    if (sl->pat == S4_BLANK) {
        s4[s].mute = 1;
        return;
    }
    s4[s].mute = 0;
    if (sl->pat != s4p.pat[s])
        s4p.want[s] = sl->pat;                 /* (the UI stages it; it goes in at the boundary) */
}
static int song_first(int s, int from)
{
    s4_song_t *g = &s4song[s4p.song % S4_NSONG];
    int k;
    for (k = 0; k < S4_NSLOT; k++) {
        int i = (from + k) % S4_NSLOT;
        if (g->slot[s][i].pat != S4_EMPTY)
            return i;
    }
    return -1;
}
static void song_start(void)
{
    int s;
    for (s = 0; s < S4_NSEQ; s++) {
        int f = song_first(s, 0);
        s4p.slot[s] = (uint8_t)(f < 0 ? 0 : f);
        s4p.reps[s] = f < 0 ? 0 : s4song[s4p.song % S4_NSONG].slot[s][f].rep;
        if (f >= 0)
            song_req(s);
    }
}
static uint16_t song_loops[S4_NSEQ];
static void song_advance(int s)
{
    s4_song_t *g = &s4song[s4p.song % S4_NSONG];
    int f = song_first(s, s4p.slot[s] + 1);
    if (f < 0)
        return;
    s4p.slot[s] = (uint8_t)f;
    s4p.reps[s] = g->slot[s][f].rep;
    song_req(s);
    if (g->master == s + 1) {                  /* the master moves: the others follow to the same slot */
        int o;
        for (o = 0; o < S4_NSEQ; o++)
            if (o != s && g->slot[o][f].pat != S4_EMPTY) {
                s4p.slot[o] = (uint8_t)f;
                s4p.reps[o] = g->slot[o][f].rep;
                song_req(o);
            }
    }
}
static void song_tick(uint32_t prev)
{
    s4_song_t *g = &s4song[s4p.song % S4_NSONG];
    int s;
    for (s = 0; s < S4_NSEQ; s++) {
        int done = 0;
        s4_slot_t *sl = &g->slot[s][s4p.slot[s] % S4_NSLOT];
        if (sl->pat == S4_EMPTY)
            continue;
        if (g->launchq || sl->pat == S4_BLANK) {
            uint32_t bars = sl->pat == S4_BLANK ? 1u : g->launchq;
            done = at_boundary(prev, gpos, bars);
        } else if (rt[s].c[0].loops != song_loops[s]) {
            done = 1;
        }
        song_loops[s] = rt[s].c[0].loops;
        if (!done || (g->master && g->master != s + 1))
            continue;
        if (sl->rep && s4p.reps[s] > 0)
            s4p.reps[s]--;
        if (sl->rep && s4p.reps[s] == 0)
            song_advance(s);
    }
}
