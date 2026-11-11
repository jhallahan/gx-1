/* SPDX-License-Identifier: GPL-3.0-only */
/* GX-1: the genre engine's compositions as SEQ-4 patterns.
 *
 * The engine (firmware/src/gx/, from jhallahan/gx-1-engine) renders a composition (drums, chords, a sub,
 * an upper part, a CC lane) into events. This bridge writes them into the staging buffers of the four
 * sequencers and queues them, so SEQ-4 plays them, edits them and saves them like any pattern, and swaps
 * them in at its next boundary (at once when stopped):
 *
 *   sequencer 1  drums   Poly, the drum map on (each note's degree is a drum sound: ADR-024)
 *   sequencer 2  sub     Mono
 *   sequencer 3  chords  Poly
 *   sequencer 4  upper   Mono (arp, mid bass or motif)
 *   the CC lane          mod lane 1 of the sequencer it targets
 *
 * One 16th is one step (SEQ-4's division 1/16 is 960 units, the engine's step). An event's offset from its
 * step becomes the step's timing offset (% of a step); a note longer than a step becomes tied steps; a
 * ratchet becomes the step's repeats; a live chance becomes the step's chance. Pitched parts use the
 * chromatic scale from C0 (24), so a degree is a MIDI note minus 24. Phrases are 8 bars: 128 steps. */
#include "gx.h"
#include "gx_profile.h"

#define GXB_MAXEV 1024u
#define GXB_BASE 24                            /* chromatic, root C, octave 0: seq_base() = 24 */
#define GXB_BARS 8u

static gx_event_t gxb_ev[GXB_MAXEV];
static gx_comp_t gxb_comp;
static gx_history_t gxb_hist;
static int gxb_ok;
static gx_map_id_t gxb_map = GX_MAP_TONVERK_SPLIT;
static uint32_t gxb_last_n;                    /* events in the last render (status) */
static gx_harmony_t gxb_harm;                  /* the last render's chords, per bar (the GEN page shows them) */
static int gxb_harm_ok;
static int gxb_set_tempo;                      /* the next render sets the tempo (a new genre only: ADR-028) */
static int gxb_now;                            /* the next render comes in at the next bar, from its start (a new idea) */

static int clampb(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a sequencer made ready for generated material: its settings kept, its steps and lanes cleared */
static void stage_base(int s, int mode, int ch)
{
    s4_seq_t *q = &s4_stage[s];
    *q = s4[s];
    memset(&q->d, 0, sizeof q->d);
    memset(q->mod, 0, sizeof q->mod);
    memset(q->ml, 0, sizeof q->ml);
    memset(q->lfo, 0, sizeof q->lfo);          /* generated parts start from clean modulation */
    q->mode = (uint8_t)mode;
    q->ch = (uint8_t)ch;
    q->port = 0;
    q->scale = 0;                              /* chromatic */
    q->root = 0;
    q->oct = 0;
    q->swing = 50;                             /* the engine's swing is in each step's offset */
    q->div = DV_16;
    q->dir = D_FWD;
    q->rdiv = 0;
    q->init = 0;
    q->end = (uint8_t)(GXB_BARS * 16u - 1u);
    q->loop_on = 0;
    q->rp_vel = q->rp_oct = 0;
    q->rp_trig = 100;
    q->rp_rtg = 0;
    q->poly_max = S4_POLY;
    q->arp.mode = 0;
    q->rsv[0] = 0;
}

/* the step an event starts on, and its offset (% of a step, -45..45) */
static int ev_step(const gx_event_t *e, int *ofs)
{
    int st = (int)((e->t + GX_STEP / 2u) / GX_STEP);
    *ofs = clampb(((int)e->t - st * (int)GX_STEP) * 100 / (int)GX_STEP, -45, 45);
    return st;
}

/* how a note splits into steps: the count, and the last one's gate. A gate is a % of a step from where
 * the note starts (its offset included), so a note within one step keeps its own length; a longer note is
 * tied steps, the last one's gate from that step's grid start. Drums and notes with a chance are never tied:
 * a tied continuation has no chance of its own and would play when the first step's chance fails. */
static int ev_steps(const gx_event_t *e, int st, int *last_gate)
{
    int32_t end = (int32_t)(e->t + e->len) - st * (int32_t)GX_STEP;   /* from the step's grid start */
    int n = 1;
    if (e->role == GX_ROLE_DRUMS || e->cond || e->len <= GX_STEP) {
        *last_gate = clampb((int)(e->len * 100u / GX_STEP), 1, 100);
        return 1;
    }
    while (end > n * (int32_t)GX_STEP + (int32_t)GX_STEP / 16)
        n++;
    *last_gate = clampb((int)((end - (n - 1) * (int32_t)GX_STEP) * 100 / (int32_t)GX_STEP), 1, 100);
    return n;
}

static int poly_put(s4_seq_t *q, const s4_pnote_t *pn)
{
    int i, cnt = 0, at;
    for (i = 0; i < q->d.poly.n; i++)
        if (q->d.poly.note[i].step == pn->step)
            cnt++;
    if (cnt >= S4_POLY || q->d.poly.n >= S4_NPN)
        return -1;
    for (at = q->d.poly.n; at > 0 && q->d.poly.note[at - 1].step > pn->step; at--)
        q->d.poly.note[at] = q->d.poly.note[at - 1];
    q->d.poly.note[at] = *pn;
    q->d.poly.n++;
    return 0;
}

/* a note into a Poly sequencer: tied steps for its length */
static void poly_note(s4_seq_t *q, const gx_event_t *e, int deg)
{
    int ofs, st = ev_step(e, &ofs), last, n = ev_steps(e, st, &last), k, i;
    for (i = 0; st > 0 && i < q->d.poly.n; i++)     /* the same note tied into this step: it retriggers */
        if (q->d.poly.note[i].step == st - 1 && q->d.poly.note[i].deg == deg && q->d.poly.note[i].gate == S4_TIE)
            q->d.poly.note[i].gate = 95;
    for (k = 0; k < n && st + k < (int)S4_NSTEP; k++) {
        s4_pnote_t pn;
        pn.step = (uint8_t)(st + k);
        pn.deg = (int8_t)deg;
        pn.vel = e->vel;
        pn.gate = (uint8_t)(k < n - 1 ? S4_TIE : last);
        pn.oct = 0;
        pn.cond = (uint8_t)(k == 0 && e->cond ? e->cond : 100);
        pn.rtg = (uint8_t)(k == 0 && e->rep > 1 ? (e->rep - 1u) & S4_RTG_N : 0);
        pn.ofs = (int8_t)(k == 0 ? ofs : 0);
        if (poly_put(q, &pn))
            return;
    }
}

/* a note into a Mono sequencer: tied steps for its length (a later note overwrites) */
static void mono_note(s4_seq_t *q, const gx_event_t *e)
{
    int ofs, st = ev_step(e, &ofs), last, n = ev_steps(e, st, &last), k;
    if (st > 0 && q->d.mono[st - 1].gate == S4_TIE && q->d.mono[st - 1].deg == (int)e->note - GXB_BASE)
        q->d.mono[st - 1].gate = 95;               /* the same note tied into this step: it retriggers */
    else if (st > 0 && q->d.mono[st - 1].vel && q->d.mono[st - 1].gate == S4_TIE)
        q->d.mono[st - 1].gate = S4_LEG;           /* another note tied into this step: legato (they overlap, so a
                                                      synth's glide hears it) */
    for (k = 0; k < n && st + k < (int)S4_NSTEP; k++) {
        s4_step_t *x = &q->d.mono[st + k];
        x->deg = (int8_t)((int)e->note - GXB_BASE);
        x->vel = e->vel;
        x->gate = (uint8_t)(k < n - 1 ? S4_TIE : last);
        x->oct = 0;
        x->cond = (uint8_t)(k == 0 && e->cond ? e->cond : 100);
        x->rtg = (uint8_t)(k == 0 && e->rep > 1 ? (e->rep - 1u) & S4_RTG_N : 0);
        x->ofs = (int8_t)(k == 0 ? ofs : 0);
        x->glide = (uint8_t)(k == 0 && (e->flags & GX_EF_GLIDE) ? 40 : 0);
    }
}

/* render the composition and stage it on the four sequencers; -1 if it did not fit */
static int gxb_apply(void)
{
    static const int SEQ_OF[GX_NROLE] = { 0, 1, 2, 3 };
    const gx_outmap_t *map = gx_outmap(gxb_map);
    gx_render_t r;
    uint32_t i;
    int s, l, cc_on[S4_NSEQ] = { 0, 0, 0, 0 };
    r.ev = gxb_ev;
    r.cap = GXB_MAXEV;
    r.n = 0;
    r.dropped = 0;
    if (!map || gx_render(&gxb_comp, map, &r) != 0 || r.dropped)
        return -1;
    gxb_last_n = r.n;
    for (l = 0; l < (int)GX_NLANE; l++) {      /* the drum map: kicks and kit where the map says */
        s4_dm_note[l] = map->lane_note[l];
        s4_dm_ch[l] = map->lane_ch[l];
    }
    stage_base(SEQ_OF[GX_ROLE_DRUMS], M_POLY, map->lane_ch[GX_LANE_SNARE]);
    s4_stage[SEQ_OF[GX_ROLE_DRUMS]].rsv[0] = S4_RSV_DRUMMAP;
    stage_base(SEQ_OF[GX_ROLE_SUB], M_MONO, map->ch[GX_ROLE_SUB]);
    stage_base(SEQ_OF[GX_ROLE_CHORDS], M_POLY, map->ch[GX_ROLE_CHORDS]);
    stage_base(SEQ_OF[GX_ROLE_UPPER], M_MONO, map->ch[GX_ROLE_UPPER]);

    for (i = 0; i < r.n; i++) {
        const gx_event_t *e = &r.ev[i];
        int ofs, st;
        if (e->t >= GXB_BARS * GX_BAR)
            continue;
        switch (e->role) {
        case GX_ROLE_DRUMS:
            poly_note(&s4_stage[SEQ_OF[GX_ROLE_DRUMS]], e, e->lane);
            break;
        case GX_ROLE_CHORDS:
            poly_note(&s4_stage[SEQ_OF[GX_ROLE_CHORDS]], e, (int)e->note - GXB_BASE);
            break;
        case GX_ROLE_SUB:
        case GX_ROLE_UPPER:
            mono_note(&s4_stage[SEQ_OF[e->role]], e);
            break;
        case GX_ROLE_CC:                       /* mod lane 1 of the target: the value held until the next */
            s = SEQ_OF[e->lane < GX_NROLE ? e->lane : GX_ROLE_UPPER];
            st = ev_step(e, &ofs);
            if (!cc_on[s]) {
                s4_modl_t *m = &s4_stage[s].ml[0];
                m->on = 1;
                m->xtype = X_CC;
                m->xnum = e->note;
                m->mx = 127;
                m->mn = 0;
                m->linked = 1;                 /* it steps with the sequence */
                m->smooth = 3;
                m->div = DV_16;
                m->init = 0;
                m->end = (uint8_t)(GXB_BARS * 16u - 1u);
                cc_on[s] = 1;
            }
            for (l = st; l < (int)(GXB_BARS * 16u); l++)
                s4_stage[s].mod[0][l] = e->vel;
            break;
        default:
            break;
        }
    }
    {
        const gx_genre_prof_t *hp = gx_profile((gx_genre_t)gxb_comp.r[GX_ROLE_CHORDS].genre);
        gxb_harm_ok = hp != 0;
        if (hp)
            gx_harmony_build(&gxb_comp.r[GX_ROLE_CHORDS], &gxb_comp, hp, &gxb_harm);
    }
    if (gxb_set_tempo)                         /* only a new genre sets the tempo; the player's stays */
        s4g.tempo = gxb_comp.bpm_x10;
    gxb_set_tempo = 0;
    for (s = 0; s < S4_NSEQ; s++)
        if (gxb_now)
            s4_queue_pattern_bar(s, s4p.pat[s]);
        else
            s4_queue_pattern(s, s4p.pat[s]);
    gxb_now = 0;
    return (int)r.n;
}

/* ---- the operations (UI, SysEx, the host simulator) ---- */

static void gxb_push(void) { gx_hist_push(&gxb_hist, &gxb_comp); }

/* per role: the macros it has taken from GEN (a bit each); GEN's macros leave those alone */
static uint8_t gxb_own[GX_NROLE];
static int gxb_new(int genre, uint32_t seed)
{
    int r;
    for (r = 0; r < (int)GX_NROLE; r++)
        gxb_own[r] = 0;
    if (genre < 1 || genre >= (int)GX_NGENRE)
        return -1;
    gx_comp_init(&gxb_comp, (gx_genre_t)genre, seed, gx_genre_tempo((gx_genre_t)genre), (uint8_t)GXB_BARS);
    gx_hist_init(&gxb_hist, &gxb_comp);
    gxb_ok = 1;
    gxb_set_tempo = 1;
    gxb_now = 1;
    return gxb_apply();
}
static int gxb_ready(void) { return gxb_ok ? 0 : gxb_new(GX_GENRE_DEEP_DNB, 1) < 0 ? -1 : 0; }
static int gxb_compose(uint32_t seed) { if (gxb_ready()) return -1; gx_compose(&gxb_comp, seed); gxb_push(); gxb_now = 1; return gxb_apply(); }
static int gxb_part(int slot)
{
    if (gxb_ready() || slot < 0 || slot >= (int)GX_NPART)
        return -1;
    gxb_comp.part = (uint8_t)slot;
    gxb_now = 1;                               /* a part comes in at the next bar, from its start */
    return gxb_apply();
}
static int gxb_lock(int role, int on) { if (gxb_ready()) return -1; gx_lock(&gxb_comp, (gx_role_t)role, on); gxb_push(); return gxb_apply(); }
static int gxb_reseed(int role, uint32_t seed) { if (gxb_ready()) return -1; gx_reseed(&gxb_comp, (gx_role_t)role, seed); gxb_push(); return gxb_apply(); }
static int gxb_mutate(int role, int mask, int intensity, uint32_t seed)
{
    if (gxb_ready() || gx_mutate(&gxb_comp, (gx_role_t)role, (uint8_t)mask, (uint8_t)intensity, seed))
        return -1;
    gxb_push();
    return gxb_apply();
}
static int gxb_macro(int m, int v)
{
    int r;
    if (gxb_ready() || m < 0 || m >= (int)GX_NMACRO)
        return -1;
    for (r = 0; r < (int)GX_NROLE; r++)
        if (!(gxb_own[r] >> m & 1u))
            gxb_comp.r[r].macro[m] = (uint8_t)clampb(v, 0, 127);
    gxb_push();
    return gxb_apply();
}
/* one track's own macro (TRACK's K1-K4): it stops following GEN's */
static int gxb_macro_role(int role, int m, int v)
{
    if (gxb_ready() || role < 0 || role >= (int)GX_NROLE || m < 0 || m >= (int)GX_NMACRO)
        return -1;
    gxb_comp.r[role].macro[m] = (uint8_t)clampb(v, 0, 127);
    gxb_own[role] |= (uint8_t)(1u << m);
    gxb_push();
    return gxb_apply();
}
/* the key: every pitched part moves, the drums stay (ADR-028) */
static int gxb_key(int key)
{
    if (gxb_ready())
        return -1;
    gxb_comp.key = (uint8_t)(((key % 12) + 12) % 12);
    gxb_push();
    return gxb_apply();
}
static int gxb_undo(void) { return gxb_ok && gx_hist_undo(&gxb_hist, &gxb_comp) ? gxb_apply() : -1; }
static int gxb_redo(void) { return gxb_ok && gx_hist_redo(&gxb_hist, &gxb_comp) ? gxb_apply() : -1; }
static int gxb_set_map(int id)
{
    if (id < 0 || id >= (int)GX_NMAP)
        return -1;
    gxb_map = (gx_map_id_t)id;
    return gxb_ok ? gxb_apply() : 0;
}

#ifdef EN_HOST
/* the host simulator's `gx dump FILE`: the last render as the device should play it (tests/gx_parity.py):
 * bpm, then per event: time (units, 3840 a quarter), length, role, MIDI channel (1..16), note, velocity,
 * repeats, chance (0 = always) */
static int gxb_dump(const char *path)
{
    const gx_outmap_t *map = gx_outmap(gxb_map);
    gx_render_t r;
    FILE *f = fopen(path, "w");
    uint32_t i;
    if (!f || !map)
        return -1;
    r.ev = gxb_ev;
    r.cap = GXB_MAXEV;
    r.n = 0;
    r.dropped = 0;
    gx_render(&gxb_comp, map, &r);             /* the same render the patterns were staged from */
    fprintf(f, "bpm %u.%u\n", gxb_comp.bpm_x10 / 10u, gxb_comp.bpm_x10 % 10u);
    for (i = 0; i < r.n; i++) {
        const gx_event_t *e = &r.ev[i];
        int ch, note;
        if (e->role == GX_ROLE_CC)
            continue;
        ch = e->role == GX_ROLE_DRUMS ? s4_dm_ch[e->lane] : map->ch[e->role];
        note = e->role == GX_ROLE_DRUMS ? s4_dm_note[e->lane] : e->note;
        fprintf(f, "%u %u %u %d %d %u %u %u\n", (unsigned)e->t, (unsigned)e->len, e->role, ch + 1, note, e->vel,
                e->rep, e->cond);
    }
    {   /* and what was staged on the drums sequencer (to compare) */
        const s4_seq_t *q = &s4[0];
        int k;
        for (k = 0; k < q->d.poly.n; k++)
            fprintf(f, "# poly step %u deg %d vel %u gate %u cond %u rtg %u ofs %d\n", q->d.poly.note[k].step,
                    q->d.poly.note[k].deg, q->d.poly.note[k].vel, q->d.poly.note[k].gate, q->d.poly.note[k].cond,
                    q->d.poly.note[k].rtg, q->d.poly.note[k].ofs);
    }
    return fclose(f) == 0 ? (int)r.n : -1;
}
#endif
