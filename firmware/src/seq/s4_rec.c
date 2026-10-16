/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: recording, MIDI capture, and the notes coming in. Part of seq4.c.
 *   recording   while playing, a key (or a note coming in) is written to the step it fell in, quantized as
 *               far as asked (else its offset kept); let go, its length becomes the gate (past the step: a
 *               tie over the steps it covered). Overdub off: the steps the playhead passes are cleared.
 *               Loop, one-shot (stops at the end) or extend (the sequence grows as it goes). CCs coming in
 *               go into a mod lane that sends them.
 *   capture     the last 256 notes played without recording are kept: capture writes them into a
 *               sequencer, from the first one's step
 *   notes in    to the sequencer they are for (by channel, by channel and note in Multitrack, else the one
 *               on screen), or they transpose one, or a mapping takes them */

static uint8_t rec_on, rec_live;               /* armed; recording now */
static uint8_t rec_seq, rec_trk;
static uint8_t rec_wrote[S4_NSEQ][S4_NTRK][S4_NSTEP / 8];   /* steps written this pass (overdub off spares them) */
typedef struct {
    uint8_t s, t, note, step, on;
    uint32_t at;               /* when it began (the sequencer's time) */
    uint32_t len;              /* its step's length */
} pend_t;
static pend_t pend[16];

typedef struct {
    uint8_t note, vel, trk, rsv;
    uint32_t on, dur;          /* song units */
} cap_t;
static cap_t cap[256];
static uint32_t cap_n;
volatile uint32_t s4_cap_count;                /* notes waiting to be captured (the UI's icon) */

static int rec_armed(void) { return rec_on; }
static uint32_t count_units(void)
{
    static const uint16_t B[5] = {0, 1, 2, 4, 8};   /* beats */
    return (uint32_t)B[clampi(s4g.count_in, 0, 4)] * S4_U;
}
static void rec_set(int on)
{
    if (s4g.tcc_ch && (on != 0) != (rec_on != 0))   /* the transport channel told (CC 107), on a change only */
        out3(0, 0xB0 | ((s4g.tcc_ch - 1) & 15), 107, on ? 127 : 0);
    rec_on = (uint8_t)on;
    rec_live = (uint8_t)(on && playing);
    s4p.rec = rec_on;
    rec_seq = s4_sel;
    if (!on) {
        int k;
        for (k = 0; k < 16; k++)
            pend[k].on = 0;
    }
    {
        int s, t, b;
        for (s = 0; s < S4_NSEQ; s++)
            for (t = 0; t < S4_NTRK; t++)
                for (b = 0; b < S4_NSTEP / 8; b++)
                    rec_wrote[s][t][b] = 0;
    }
}
static void rec_stop(void) { rec_live = 0; }
static void rec_count_done(void) { rec_live = rec_on; }
void s4_rec_track(int t) { rec_trk = (uint8_t)(t & 7); }

/* the step a note falls in now, and its offset (% of the step) */
static int rec_where(int s, int t, int *ofs, uint32_t *len)
{
    rt_t *r = &rt[s];
    clk_t *c = &r->c[t & 7];
    int32_t into = (int32_t)(r->lpos - c->t);
    int pct = c->len ? (int)((int64_t)into * 100 / (int32_t)c->len) : 0, step = c->pos;
    if (s4g.rec_q)                             /* quantized: the offset shrunk by the amount */
        pct = pct * (100 - clampi(s4g.rec_qpct, 0, 100)) / 100;
    if (pct >= 50 || (s4g.rec_q && s4g.rec_qpct >= 100 && into * 2 >= (int32_t)c->len)) {
        step = c->npos;
        pct -= 100;
        if (s4g.rec_q && s4g.rec_qpct >= 100)
            pct = 0;
    }
    *ofs = clampi(pct, -45, 45);
    *len = c->len;
    return step & 127;
}

static void rec_mark(int s, int t, int step) { rec_wrote[s][t & 7][step >> 3] |= (uint8_t)(1u << (step & 7)); }

static void rec_note(int s, int t, int note, int vel)
{
    s4_seq_t *q = &s4[s];
    int k;
    if (vel && !rec_live) {                    /* not recording: kept for a capture */
        cap_t *e = &cap[cap_n % 256u];
        e->note = (uint8_t)note;
        e->vel = (uint8_t)vel;
        e->trk = (uint8_t)t;
        e->on = gpos + (playing ? 0u : rt[s].lpos);
        e->dur = 0;
        __asm__ volatile("" ::: "memory");   /* (the note in before the count says so: the UI reads it) */
        cap_n++;
        s4_cap_count = cap_n > 256u ? 256u : cap_n;
        return;
    }
    if (!vel && !rec_live) {
        uint32_t i;
        for (i = 0; i < 256u && i < cap_n; i++) {
            cap_t *e = &cap[(cap_n - 1u - i) % 256u];
            if (e->note == note && !e->dur) {
                e->dur = gpos + (playing ? 0u : rt[s].lpos) - e->on;
                if (!e->dur)
                    e->dur = 1;
                break;
            }
        }
        return;
    }
    if (!playing || s != rec_seq)
        return;
    if (vel) {
        int ofs, step;
        uint32_t len;
        if (q->mode == M_MULTI) {              /* to the armed track, or the track this note is */
            int a = rec_trk;
            for (k = 0; k < S4_NTRK; k++)
                if (q->d.multi.t[k].note == note && s4g.filt == 2)
                    a = k;
            t = a;
        }
        step = rec_where(s, t, &ofs, &len);
        switch (q->mode) {
        case M_MONO: {
            s4_step_t *st = &q->d.mono[step];
            st->deg = (int8_t)s4_deg_of(s, note);
            st->oct = 0;
            st->vel = (uint8_t)vel;
            st->gate = q->gdef ? q->gdef : 75;
            st->ofs = (int8_t)ofs;
            st->cond = 100;
            st->rtg = 0;
            break;
        }
        case M_MULTI: {
            s4_step_t *st = &q->d.multi.s[t][step];
            int rel = note - q->d.multi.t[t].note;
            st->deg = (int8_t)clampi(rel, -64, 63);
            st->vel = (uint8_t)vel;
            st->gate = q->gdef ? q->gdef : 50;
            st->ofs = (int8_t)ofs;
            st->cond = 100;
            st->rtg = 0;
            st->oct = 0;
            break;
        }
        case M_POLY: {
            s4_pnote_t pn;
            int d = s4_deg_of(s, note);
            pn.step = (uint8_t)step;
            pn.deg = (int8_t)d;
            pn.vel = (uint8_t)vel;
            pn.gate = q->gdef ? q->gdef : 75;
            pn.oct = 0;
            pn.cond = 100;
            pn.rtg = 0;
            pn.ofs = (int8_t)ofs;
            if (s4_poly_find(s, step, d) < 0)
                s4_poly_add(s, &pn);
            break;
        }
        case M_CHORD: {                        /* the chord the keyboard plays (or the note's degree) */
            s4_cstep_t *c = &q->d.chord[step];
            if (s4_kchord.vel) {
                *c = s4_kchord;
                c->vel = (uint8_t)vel;
            } else {
                c->deg = (int8_t)s4_deg_of(s, note);
                c->type = 0;
                c->voi = 0;
                c->spread = 0;
                c->vel = (uint8_t)vel;
            }
            c->gate = q->gdef ? q->gdef : 75;
            c->ofs = (int8_t)ofs;
            c->cond = 100;
            c->rtg = 0;
            break;
        }
        default:
            return;
        }
        rec_mark(s, t, step);
        for (k = 0; k < 16; k++)
            if (!pend[k].on) {
                pend[k].on = 1;
                pend[k].s = (uint8_t)s;
                pend[k].t = (uint8_t)t;
                pend[k].note = (uint8_t)note;
                pend[k].step = (uint8_t)step;
                pend[k].at = rt[s].lpos;
                pend[k].len = len;
                break;
            }
        return;
    }
    for (k = 0; k < 16; k++)                   /* let go: its length */
        if (pend[k].on && pend[k].s == s && pend[k].note == note) {
            uint32_t held = rt[s].lpos - pend[k].at;
            int g = pend[k].len ? (int)(held * 100u / pend[k].len) : 75, st = pend[k].step, n, i;
            pend[k].on = 0;
            if (g <= 100) {
                g = clampi(g, 1, 100);
                switch (q->mode) {
                case M_MONO: q->d.mono[st].gate = (uint8_t)g; break;
                case M_MULTI: q->d.multi.s[pend[k].t][st].gate = (uint8_t)g; break;
                case M_CHORD: q->d.chord[st].gate = (uint8_t)g; break;
                case M_POLY: {
                    int j = s4_poly_find(s, st, s4_deg_of(s, note));
                    if (j >= 0)
                        q->d.poly.note[j].gate = (uint8_t)g;
                    break;
                }
                default: break;
                }
            } else if (q->mode == M_MONO || q->mode == M_MULTI) {   /* longer: tied over the steps it covered */
                n = (int)(held / (pend[k].len ? pend[k].len : 1u));
                for (i = 0; i <= n && i < 127; i++) {
                    int p = (st + i) & 127;
                    s4_step_t *x = q->mode == M_MONO ? &q->d.mono[p] : &q->d.multi.s[pend[k].t][p];
                    s4_step_t *f = q->mode == M_MONO ? &q->d.mono[st] : &q->d.multi.s[pend[k].t][st];
                    if (i)
                        *x = *f;
                    x->gate = i < n ? S4_TIE : (uint8_t)clampi((int)((held % pend[k].len) * 100u / pend[k].len), 1, 100);
                    x->ofs = i ? 0 : x->ofs;
                    rec_mark(s, pend[k].t, p);
                }
            } else {
                switch (q->mode) {
                case M_CHORD: q->d.chord[st].gate = S4_TIE; break;
                case M_POLY: {
                    int j = s4_poly_find(s, st, s4_deg_of(s, note));
                    if (j >= 0)
                        q->d.poly.note[j].gate = 100;
                    break;
                }
                default: break;
                }
            }
            break;
        }
}

/* a step begins while recording: overdub off clears what was there (not what was just written); a
 * one-shot stops at the end; extend grows the sequence */
static void rec_commit(int s, int t, int pos)
{
    s4_seq_t *q = &s4[s];
    int lo, hi;
    if (!rec_live || s != rec_seq)
        return;
    if (q->mode == M_MULTI && t != rec_trk)
        return;
    if (q->mode != M_MULTI && t)
        return;
    trk_range(s, t, &lo, &hi);
    if (s4g.rec_mode == 1 && pos == lo && rt[s].c[t].loops > 0) {   /* one-shot: once round */
        rec_on = 0;
        rec_live = 0;
        s4p.rec = 0;
        return;
    }
    if (s4g.rec_mode == 2 && pos == hi && hi < 127) {   /* extend: one more step */
        if (q->mode == M_MULTI)
            q->d.multi.t[t].end++;
        else
            q->end++;
    }
    if (!s4g.rec_over && !(rec_wrote[s][t][pos >> 3] >> (pos & 7) & 1u)) {
        switch (q->mode) {
        case M_MONO: q->d.mono[pos].vel = 0; break;
        case M_MULTI: q->d.multi.s[t][pos].vel = 0; break;
        case M_CHORD: q->d.chord[pos].vel = 0; break;
        case M_POLY: {
            int i;
            for (i = q->d.poly.n - 1; i >= 0; i--)
                if (q->d.poly.note[i].step == pos)
                    s4_poly_del(s, i);
            break;
        }
        default: break;
        }
    }
}

static void rec_cc(int ch, int kind, int d1, int d2)   /* motion: a CC (bend, pressure) into its lane */
{
    int s = rec_seq, l;
    s4_seq_t *q = &s4[s];
    if (!rec_live)
        return;
    if (s4g.filt && ch != q->ch)
        return;
    for (l = 0; l < lanes_in_use(s); l++) {
        s4_modl_t *m = &q->ml[l];
        int v = -1;
        if (kind == 0xB0 && m->xtype == X_CC && m->xnum == d1)
            v = d2;
        else if (kind == 0xE0 && m->xtype == X_PB)
            v = d2;
        else if (kind == 0xD0 && m->xtype == X_AT)
            v = d1;
        if (v >= 0) {
            q->mod[l][lane_step(s, l) & 127] = (uint8_t)v;
            m->on = 1;
        }
    }
}

/* capture: the notes played without recording, into a sequencer from the first one's step */
/* the notes played lately, for the UI (step recording, the hand): the i-th since boot, 0 if gone */
uint32_t s4_cap_total(void) { return cap_n; }
int s4_cap_get(uint32_t i, uint8_t *note, uint8_t *vel, uint8_t *trk)
{
    uint32_t n = cap_n;
    const cap_t *e = &cap[i % 256u];
    if (i >= n || n - i > 255u)
        return 0;
    *note = e->note;
    *vel = e->vel;
    *trk = e->trk;
    return cap_n - i <= 255u;
}

static void capture_into(int s)
{
    s4_seq_t *q = &s4[s];
    uint32_t n = cap_n > 256u ? 256u : cap_n, i, u = s4_div_units(q->div), first, base;
    int maxstep = 0;
    if (!n || !u)
        return;
    first = cap[(cap_n - n) % 256u].on;
    base = first / u * u;                      /* from the step the first note fell in */
    s4_clear(s, -1, 0);
    for (i = 0; i < n; i++) {
        cap_t *e = &cap[(cap_n - n + i) % 256u];
        int step = (int)((e->on - base + u / 2u) / u);
        int g = e->dur ? (int)(e->dur * 100u / u) : 75;
        if (step > 127)
            break;
        if (step > maxstep)
            maxstep = step;
        switch (q->mode) {
        case M_MONO: {
            s4_step_t *st = &q->d.mono[step];
            st->deg = (int8_t)s4_deg_of(s, e->note);
            st->oct = 0;
            st->vel = e->vel;
            st->gate = (uint8_t)clampi(g, 1, 100);
            st->cond = 100;
            st->rtg = 0;
            st->ofs = 0;
            break;
        }
        case M_MULTI: {
            int t = e->trk & 7;
            s4_step_t *st = &q->d.multi.s[t][step];
            st->deg = (int8_t)clampi(e->note - q->d.multi.t[t].note, -64, 63);
            st->vel = e->vel;
            st->gate = (uint8_t)clampi(g, 1, 100);
            st->cond = 100;
            st->rtg = 0;
            st->ofs = 0;
            st->oct = 0;
            if (step > q->d.multi.t[t].end)
                q->d.multi.t[t].end = (uint8_t)step;
            break;
        }
        case M_POLY: {
            s4_pnote_t pn;
            pn.step = (uint8_t)step;
            pn.deg = (int8_t)s4_deg_of(s, e->note);
            pn.vel = e->vel;
            pn.gate = (uint8_t)clampi(g, 1, 100);
            pn.oct = 0;
            pn.cond = 100;
            pn.rtg = 0;
            pn.ofs = 0;
            if (s4_poly_find(s, step, pn.deg) < 0)
                s4_poly_add(s, &pn);
            break;
        }
        case M_CHORD: {
            s4_cstep_t *c = &q->d.chord[step];
            if (!c->vel) {
                c->deg = (int8_t)s4_deg_of(s, e->note);
                c->vel = e->vel;
                c->gate = (uint8_t)clampi(g, 1, 100);
                c->cond = 100;
            }
            break;
        }
        default: break;
        }
    }
    if (q->mode != M_MULTI) {                  /* the length: whole pages of what was played */
        q->init = 0;
        q->end = (uint8_t)clampi((maxstep / 16 + 1) * 16 - 1, 15, 127);
    }
    cap_n = 0;
    s4_cap_count = 0;
}

/* --------------------------------------------------------------------------------- notes in --- */
static void midi_note_in(int cable, int st, int note, int vel)
{
    int ch = st & 15, s, t, any = 0, play;
    /* transpose: a sequencer that takes its transposition from this channel */
    for (s = 0; s < S4_NSEQ; s++) {
        int tp = s4[s].tpose;
        if (!tp || !(tp == 1 ? ch == s4[s].ch : ch == tp - 2))
            continue;
        if (vel)
            rt[s].tpose = (int8_t)clampi(note - seq_base(s), -48, 48);
        any = 1;
    }
    if (any)
        return;
    for (s = 0; s < S4_NSEQ; s++)
        if (map_note(s, ch, note, vel))
            return;
    /* thru took it raw: here it is only used (recording, the arpeggiator); else it plays through */
    play = (cable == 14 && s4g.thru) || (cable < 4 && s4g.usb_thru != 1) ? 0 : 1;
    if (cable < 4 && s4g.usb_thru == 0)
        play = 0;
    if (s4g.kq && vel) {                       /* quantized to the scale of the sequencer it goes to */
        int d = s4_deg_of(s4_sel, note);
        note = s4_note_of(s4_sel, d, 0);
    }
    if (!s4g.filt) {                           /* unfiltered: the sequencer on screen takes it */
        live_note(s4_sel, s4[s4_sel].mode == M_MULTI ? rec_trk : 0, note, vel | (play ? 0 : 0x80));
        return;
    }
    for (s = 0; s < S4_NSEQ; s++) {
        s4_seq_t *q = &s4[s];
        if (q->mode == M_MULTI) {              /* by the tracks' channels (and notes) */
            for (t = 0; t < S4_NTRK; t++)
                if (q->d.multi.t[t].ch == ch && (s4g.filt != 2 || q->d.multi.t[t].note == note)) {
                    live_note(s, t, note, vel | (play ? 0 : 0x80));
                    return;
                }
            continue;
        }
        if (q->ch == ch) {
            live_note(s, 0, note, vel | (play ? 0 : 0x80));
            return;
        }
    }
}
