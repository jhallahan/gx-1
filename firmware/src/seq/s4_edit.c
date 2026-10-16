/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: editing, generating, undoing, and a pattern as bytes. Part of seq4.c. All of this runs in the
 * main loop (the UI's), on s4[s]; the engine reads the same steps as they change.
 *   defaults     a new sequencer in each mode (Multitrack: General MIDI drums on channel 10)
 *   edits        clear, rotate, transpose, expand (twice the steps, half the division), condense, duplicate
 *   generators   Euclid (a track's hits spread evenly), the random generator (a melody or a rhythm from a
 *                centre, a range, a variety and a density; or humanize, shuffle the order, shuffle the
 *                pitches, invert); fixed and locked steps are left alone
 *   undo         the last ten changes, each a sequencer as bytes
 *   bytes        a pattern packed: its settings as they are, its mod lanes run-length coded, only the steps
 *                that have notes */

s4_cstep_t s4_kchord;                          /* the chord the chord keyboard plays (recorded as it is) */

static void zero(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = 0;
}
static void copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
}

/* ------------------------------------------------------------------------------- defaults --- */
static void arp_defaults(s4_arp_t *a)
{
    zero(a, sizeof *a);
    a->gate = 50;
    a->div = DV_16;
    a->len = 16;
    a->puls = 16;
    a->rpt = 1;
}
static void mode_defaults(int s, int mode)
{
    s4_seq_t *q = &s4[s];
    int t, l, k;
    zero(&q->d, sizeof q->d);
    q->mode = (uint8_t)mode;
    switch (mode) {
    case M_MULTI: {
        static const uint8_t GM[S4_NTRK] = {36, 38, 42, 46, 39, 45, 50, 49};   /* kick snare hats .. crash */
        for (t = 0; t < S4_NTRK; t++) {
            s4_trk_t *k2 = &q->d.multi.t[t];
            k2->ch = 9;
            k2->port = q->port;
            k2->note = GM[t];
            k2->init = 0;
            k2->end = 15;
            k2->div = DV_16;
            k2->rnd_on = 1;
            k2->lfo_on = 3;
            k2->eu_len = 16;
            k2->eu_puls = 4;
            k2->dg_dens = 50;
            k2->rp_trig = 100;
        }
        for (l = 0; l < S4_NMOD; l++) {
            q->ml[l].xtype = X_CC;
            q->ml[l].xnum = 1;
        }
        break;
    }
    case M_MATRIX:
        for (t = 0; t < S4_NMX; t++) {
            s4_mxtrk_t *x = &q->d.mx[t];
            x->ch = q->ch;
            x->port = q->port;
            for (l = 0; l < S4_NLANE; l++) {
                x->lane[l].init = 0;
                x->lane[l].end = 15;
                x->lane[l].div = DV_16;
                x->lane[l].rand = 50;
            }
            x->lane[L_INTV].min = (uint8_t)(int8_t)-7;
            x->lane[L_INTV].max = 7;
            for (k = 0; k < 16; k++) {
                x->v[L_TRIG][k] = (int8_t)(k % 4 == 0 && t == 0);
                x->v[L_VEL][k] = 100;
                x->v[L_GATE][k] = 50;
                x->v[L_PROB][k] = 100;
                x->v[L_RTG][k] = 1;
                x->v[L_PULS][k] = 1;
            }
        }
        break;
    case M_STOCH: {
        s4_stoch_t *st = &q->d.stoch;
        static const uint8_t P[12] = {7, 0, 3, 0, 4, 2, 0, 5, 0, 3, 1, 0};
        for (k = 0; k < 12; k++)
            st->prob[k] = P[k];
        st->pitch = 4;
        st->rhythm = 4;
        st->next = 5;
        break;
    }
    case M_POLY:
        q->poly_max = S4_POLY;
        break;
    default:
        break;
    }
}
void s4_defaults_seq(int s, int mode)
{
    s4_seq_t *q = &s4[s];
    int l, i;
    static const uint8_t CC[4] = {1, 74, 71, 10};   /* mod wheel, cutoff, resonance, pan */
    zero(q, sizeof *q);
    q->ch = (uint8_t)s;
    q->port = 0;
    q->color = (uint8_t)s;
    q->scale = 0;
    q->root = 0;
    q->oct = 2;
    q->swing = 50;
    q->div = DV_16;
    q->init = 0;
    q->end = 15;
    q->vdef = s4g.vdef ? s4g.vdef : 100;
    q->gdef = s4g.gdef ? s4g.gdef : 75;
    q->rp_trig = 100;
    q->rg_range = -7;
    q->rg_rand = 50;
    q->rg_dens = 60;
    q->rg_human = 20;
    q->ch_auto = 1;
    q->poly_max = S4_POLY;
    q->dg_x = 128;
    q->dg_y = 128;
    q->follow = 1;
    arp_defaults(&q->arp);
    arp_defaults(&q->karp);
    for (i = 0; i < S4_NLFO; i++) {
        q->lfo[i].wave = W_SINE;
        q->lfo[i].rate = 4;                    /* a bar */
        q->lfo[i].xnum = 1;
    }
    for (l = 0; l < S4_NMOD; l++) {
        s4_modl_t *m = &q->ml[l];
        m->xtype = X_CC;
        m->xnum = CC[l & 3];
        m->mx = 127;
        m->mn = 0;
        m->linked = 1;
        m->init = 0;
        m->end = 127;
        m->div = 0;
        m->smooth = s4g.smooth;                /* (the setup's: a new sequencer's lanes) */
    }
    mode_defaults(s, mode);
}
void s4_set_mode(int s, int mode)
{
    if (s4[s].mode == mode)
        return;
    s4_undo_push(s);
    mode_defaults(s, mode);
    s4_load_now(s);
}

/* -------------------------------------------------------------------------------- Poly notes --- */
int s4_poly_find(int s, int step, int deg)
{
    int i;
    for (i = 0; i < s4[s].d.poly.n; i++)
        if (s4[s].d.poly.note[i].step == step && s4[s].d.poly.note[i].deg == deg)
            return i;
    return -1;
}
void s4_poly_del(int s, int i)
{
    s4_seq_t *q = &s4[s];
    int k;
    if (i < 0 || i >= q->d.poly.n)
        return;
    for (k = i; k < q->d.poly.n - 1; k++)
        q->d.poly.note[k] = q->d.poly.note[k + 1];
    q->d.poly.n--;
}
int s4_poly_add(int s, const s4_pnote_t *n)
{
    s4_seq_t *q = &s4[s];
    int i, cnt = 0, first = -1, at;
    for (i = 0; i < q->d.poly.n; i++)
        if (q->d.poly.note[i].step == n->step) {
            if (first < 0)
                first = i;
            cnt++;
        }
    if (cnt >= S4_POLY && first >= 0)          /* an eighth note pushes out the first */
        s4_poly_del(s, first);
    if (q->d.poly.n >= S4_NPN)
        return -1;
    for (at = q->d.poly.n; at > 0 && q->d.poly.note[at - 1].step > n->step; at--)
        q->d.poly.note[at] = q->d.poly.note[at - 1];
    q->d.poly.note[at] = *n;
    q->d.poly.n++;
    return at;
}

/* ------------------------------------------------------------------------------------- undo --- */
#define UNDO_ARENA 32768u
#define UNDO_N 20
static uint8_t undo_mem[UNDO_ARENA] S4_POOL;
typedef struct {
    uint32_t off, len;
    uint8_t s, redo;
} undo_e;
static undo_e ue[UNDO_N];
static int ue_n;
static uint32_t undo_top(void) { return ue_n ? ue[ue_n - 1].off + ue[ue_n - 1].len : 0; }
static void undo_drop_oldest(void)
{
    uint32_t cut = ue[0].len, i;
    int k;
    for (i = cut; i < undo_top(); i++)
        undo_mem[i - cut] = undo_mem[i];
    for (k = 1; k < ue_n; k++) {
        ue[k - 1] = ue[k];
        ue[k - 1].off -= cut;
    }
    ue_n--;
}
uint8_t s4_packbuf[12288] S4_POOL;            /* a sequencer packed (the project's saves use it too) */
#define pack_tmp s4_packbuf
static uint8_t pack_keep[12288] S4_POOL;      /* (an undo's bytes while the state now is packed) */
static int undo_add(int s, int redo)
{
    uint32_t n = s4_pack(&s4[s], pack_tmp, sizeof pack_tmp), undos = 0;
    int k;
    if (!n)
        return -1;
    for (k = 0; k < ue_n; k++)
        undos += !ue[k].redo;
    while (ue_n && (ue_n >= UNDO_N || undo_top() + n > UNDO_ARENA || (!redo && undos >= 10))) {
        if (!ue[0].redo)
            undos--;
        undo_drop_oldest();
    }
    if (undo_top() + n > UNDO_ARENA)
        return -1;
    ue[ue_n].off = undo_top();
    ue[ue_n].len = n;
    ue[ue_n].s = (uint8_t)s;
    ue[ue_n].redo = (uint8_t)redo;
    copy(&undo_mem[ue[ue_n].off], pack_tmp, n);
    ue_n++;
    return 0;
}
void s4_undo_push(int s)
{
    while (ue_n && ue[ue_n - 1].redo)          /* a new change: the redos go */
        ue_n--;
    undo_add(s, 0);
}
static s4_seq_t undo_seq S4_POOL;
static void swap_in(int s, const uint8_t *b, uint32_t n)
{
    if (s4_unpack(&undo_seq, b, n))
        return;
    s4_lock(1);
    copy(&s4[s], &undo_seq, sizeof undo_seq);
    s4_lock(0);
}
int s4_undo(void)
{
    int k;
    for (k = ue_n - 1; k >= 0 && ue[k].redo; k--)
        ;
    if (k < 0)
        return -1;
    {
        undo_e e = ue[k];
        int s = e.s, j;
        uint8_t *src = &undo_mem[e.off];
        /* the state now becomes a redo; the undo entry is consumed */
        copy(pack_tmp, src, e.len);
        for (j = k; j < ue_n - 1; j++)
            ue[j] = ue[j + 1];
        ue_n--;
        {   /* close the gap it left */
            uint32_t i, top = 0;
            for (j = 0; j < ue_n; j++)
                top = top > ue[j].off + ue[j].len ? top : ue[j].off + ue[j].len;
            for (j = k; j < ue_n; j++) {
                uint32_t from = ue[j].off, to = j ? ue[j - 1].off + ue[j - 1].len : 0;
                for (i = 0; i < ue[j].len; i++)
                    undo_mem[to + i] = undo_mem[from + i];
                ue[j].off = to;
            }
            (void)top;
        }
        copy(pack_keep, pack_tmp, e.len);
        undo_add(s, 1);
        swap_in(s, pack_keep, e.len);
        s4_load_now(s);
        return s;
    }
}
int s4_redo(void)
{
    int s;
    if (!ue_n || !ue[ue_n - 1].redo)
        return -1;
    {
        undo_e e = ue[ue_n - 1];
        s = e.s;
        copy(pack_keep, &undo_mem[e.off], e.len);
        ue_n--;
        undo_add(s, 0);
        swap_in(s, pack_keep, e.len);
        s4_load_now(s);
    }
    return s;
}

/* --------------------------------------------------------------------------------- the steps --- */
static s4_step_t *note_steps(int s, int t)     /* Mono's steps, or a Multitrack track's */
{
    if (s4[s].mode == M_MULTI)
        return s4[s].d.multi.s[t & 7];
    if (s4[s].mode == M_MONO)
        return s4[s].d.mono;
    if (s4[s].mode == M_STOCH)
        return s4[s].d.stoch.buf;
    return 0;
}
static int is_locked(uint8_t cond) { return cond == S4_C_FIXED || cond == S4_C_LOCKED; }

void s4_clear(int s, int trk, int full)
{
    s4_seq_t *q = &s4[s];
    int t, i;
    switch (q->mode) {
    case M_MULTI:
        for (t = 0; t < S4_NTRK; t++)
            if (trk < 0 || t == trk) {
                for (i = 0; i < S4_NSTEP; i++)
                    q->d.multi.s[t][i].vel = 0;
                if (full) {
                    s4_trk_t *k = &q->d.multi.t[t];
                    k->init = 0;
                    k->end = 15;
                    k->div = DV_16;
                    k->dir = D_FWD;
                    k->rdiv = 0;
                    k->loop_on = 0;
                    k->rp_vel = k->rp_gate = k->rp_rtg = 0;
                    k->rp_trig = 100;
                    k->dg_on = 0;
                }
            }
        break;
    case M_POLY: q->d.poly.n = 0; break;
    case M_CHORD:
        for (i = 0; i < S4_NSTEP; i++)
            q->d.chord[i].vel = 0;
        break;
    case M_STOCH:
        for (i = 0; i < S4_NSTEP; i++)
            q->d.stoch.buf[i].vel = 0;
        break;
    case M_MONO:
        for (i = 0; i < S4_NSTEP; i++)
            q->d.mono[i].vel = 0;
        break;
    default: {
        int tt, l, k;
        for (tt = 0; tt < S4_NMX; tt++)
            if (trk < 0 || tt == trk)
                for (l = 0; l < S4_NLANE; l++)
                    for (k = 0; k < 16; k++)
                        q->d.mx[tt].v[l][k] = (int8_t)(l == L_TRIG ? 0 : q->d.mx[tt].v[l][k]);
        break;
    }
    }
    if (full && q->mode != M_MULTI) {
        q->init = 0;
        q->end = 15;
        q->div = DV_16;
        q->dir = D_FWD;
        q->rdiv = 0;
        q->loop_on = 0;
        q->swing = 50;
        q->rp_vel = q->rp_oct = q->rp_rtg = 0;
        q->rp_trig = 100;
    }
    if (full)
        for (i = 0; i < S4_NMOD; i++) {
            int k;
            for (k = 0; k < S4_NSTEP; k++)
                q->mod[i][k] = 0;
        }
}

static void range_of(int s, int t, int *lo, int *hi) { trk_range(s, t, lo, hi); }

void s4_rotate(int s, int t, int by)
{
    s4_seq_t *q = &s4[s];
    int lo, hi, n, i, k;
    range_of(s, t, &lo, &hi);
    n = hi - lo + 1;
    if (n < 2 || !by)
        return;
    by = ((by % n) + n) % n;
    for (k = 0; k < by; k++) {                 /* one step right at a time (n is small) */
        switch (q->mode) {
        case M_MONO:
        case M_MULTI:
        case M_STOCH: {
            s4_step_t *st = note_steps(s, t), x = st[hi];
            for (i = hi; i > lo; i--)
                st[i] = st[i - 1];
            st[lo] = x;
            break;
        }
        case M_CHORD: {
            s4_cstep_t x = q->d.chord[hi];
            for (i = hi; i > lo; i--)
                q->d.chord[i] = q->d.chord[i - 1];
            q->d.chord[lo] = x;
            break;
        }
        case M_POLY:
            for (i = 0; i < q->d.poly.n; i++) {
                s4_pnote_t *pn = &q->d.poly.note[i];
                if (pn->step >= lo && pn->step <= hi)
                    pn->step = (uint8_t)(pn->step == hi ? lo : pn->step + 1);
            }
            {   /* sorted again */
                int a, b;
                for (a = 1; a < q->d.poly.n; a++)
                    for (b = a; b > 0 && q->d.poly.note[b - 1].step > q->d.poly.note[b].step; b--) {
                        s4_pnote_t x = q->d.poly.note[b];
                        q->d.poly.note[b] = q->d.poly.note[b - 1];
                        q->d.poly.note[b - 1] = x;
                    }
            }
            break;
        default: {
            int l, tt = t & 3;
            for (l = 0; l < S4_NLANE; l++) {
                int8_t x = q->d.mx[tt].v[l][15];
                for (i = 15; i > 0; i--)
                    q->d.mx[tt].v[l][i] = q->d.mx[tt].v[l][i - 1];
                q->d.mx[tt].v[l][0] = x;
            }
            break;
        }
        }
        if (q->mode != M_MATRIX) {             /* the mod lanes go with the steps */
            int l;
            for (l = 0; l < S4_NMOD; l++)
                if (q->ml[l].linked && (q->mode != M_MULTI || l == (t & 7))) {
                    uint8_t x = q->mod[l][hi];
                    for (i = hi; i > lo; i--)
                        q->mod[l][i] = q->mod[l][i - 1];
                    q->mod[l][lo] = x;
                }
        }
    }
}

void s4_transpose(int s, int t, int by)
{
    s4_seq_t *q = &s4[s];
    int i;
    switch (q->mode) {
    case M_MONO:
    case M_MULTI:
    case M_STOCH: {
        s4_step_t *st = note_steps(s, t);
        for (i = 0; i < S4_NSTEP; i++)
            if (st[i].vel)
                st[i].deg = (int8_t)clampi(st[i].deg + by, -64, 63);
        break;
    }
    case M_CHORD:
        for (i = 0; i < S4_NSTEP; i++)
            if (q->d.chord[i].vel)
                q->d.chord[i].deg = (int8_t)clampi(q->d.chord[i].deg + by, -64, 63);
        break;
    case M_POLY:
        for (i = 0; i < q->d.poly.n; i++)
            q->d.poly.note[i].deg = (int8_t)clampi(q->d.poly.note[i].deg + by, -64, 63);
        break;
    default:
        q->d.mx[t & 3].note = (int8_t)clampi(q->d.mx[t & 3].note + by, -48, 48);
        break;
    }
}

static int div_by_units(uint32_t u)
{
    int d;
    for (d = 0; d < DV_N; d++)
        if (s4_div_units(d) == u)
            return d;
    return -1;
}
void s4_expand(int s, int t)                   /* twice the steps at half the division, held notes tied */
{
    s4_seq_t *q = &s4[s];
    int lo, hi, n, i, nd;
    uint8_t *div = q->mode == M_MULTI ? &q->d.multi.t[t & 7].div : &q->div;
    uint8_t *end = q->mode == M_MULTI ? &q->d.multi.t[t & 7].end : &q->end;
    nd = div_by_units(s4_div_units(*div) / 2u);
    range_of(s, t, &lo, &hi);
    n = hi - lo + 1;
    if (nd < 0 || lo + 2 * n - 1 > 127)
        return;
    if (q->mode == M_MONO || q->mode == M_MULTI) {
        s4_step_t *st = note_steps(s, t);
        for (i = n - 1; i >= 0; i--) {
            s4_step_t x = st[lo + i];
            st[lo + 2 * i] = x;
            st[lo + 2 * i + 1] = x;
            if (x.vel && (x.gate > 50 || x.gate >= S4_TIE)) {
                st[lo + 2 * i].gate = S4_TIE;
                st[lo + 2 * i + 1].gate = (uint8_t)(x.gate >= S4_TIE ? x.gate : (x.gate - 50) * 2);
                st[lo + 2 * i + 1].ofs = 0;
                st[lo + 2 * i + 1].rtg = 0;
            } else {
                st[lo + 2 * i].gate = (uint8_t)clampi(x.gate * 2, 1, 100);
                st[lo + 2 * i + 1].vel = 0;
            }
        }
    } else if (q->mode == M_CHORD) {
        for (i = n - 1; i >= 0; i--) {
            s4_cstep_t x = q->d.chord[lo + i];
            q->d.chord[lo + 2 * i] = x;
            q->d.chord[lo + 2 * i + 1] = x;
            if (x.vel && x.gate > 50) {
                q->d.chord[lo + 2 * i].gate = S4_TIE;
                q->d.chord[lo + 2 * i + 1].gate = (uint8_t)(x.gate >= S4_TIE ? x.gate : (x.gate - 50) * 2);
            } else {
                q->d.chord[lo + 2 * i].gate = (uint8_t)clampi(x.gate * 2, 1, 100);
                q->d.chord[lo + 2 * i + 1].vel = 0;
            }
        }
    } else if (q->mode == M_POLY) {
        for (i = 0; i < q->d.poly.n; i++) {
            s4_pnote_t *pn = &q->d.poly.note[i];
            if (pn->step >= lo && pn->step <= hi) {
                pn->step = (uint8_t)(lo + 2 * (pn->step - lo));
                pn->gate = (uint8_t)(pn->gate >= S4_TIE ? pn->gate : clampi(pn->gate * 2, 1, 100));
            }
        }
    } else
        return;
    *div = (uint8_t)nd;
    *end = (uint8_t)(lo + 2 * n - 1);
}
void s4_condense(int s, int t)                 /* half the steps at twice the division (some lost) */
{
    s4_seq_t *q = &s4[s];
    int lo, hi, n, i, nd;
    uint8_t *div = q->mode == M_MULTI ? &q->d.multi.t[t & 7].div : &q->div;
    uint8_t *end = q->mode == M_MULTI ? &q->d.multi.t[t & 7].end : &q->end;
    nd = div_by_units(s4_div_units(*div) * 2u);
    range_of(s, t, &lo, &hi);
    n = (hi - lo + 2) / 2;
    if (nd < 0 || n < 1)
        return;
    if (q->mode == M_MONO || q->mode == M_MULTI) {
        s4_step_t *st = note_steps(s, t);
        for (i = 0; i < n; i++) {
            s4_step_t x = st[lo + 2 * i];
            if (!x.vel && lo + 2 * i + 1 <= hi)
                x = st[lo + 2 * i + 1];
            else if (x.gate == S4_TIE)
                x.gate = 100;
            st[lo + i] = x;
        }
        for (i = lo + n; i <= hi; i++)
            st[i].vel = 0;
    } else if (q->mode == M_CHORD) {
        for (i = 0; i < n; i++)
            q->d.chord[lo + i] = q->d.chord[lo + 2 * i];
        for (i = lo + n; i <= hi; i++)
            q->d.chord[i].vel = 0;
    } else if (q->mode == M_POLY) {
        for (i = q->d.poly.n - 1; i >= 0; i--) {
            s4_pnote_t *pn = &q->d.poly.note[i];
            if (pn->step >= lo && pn->step <= hi) {
                if ((pn->step - lo) & 1)
                    s4_poly_del(s, i);
                else
                    pn->step = (uint8_t)(lo + (pn->step - lo) / 2);
            }
        }
    } else
        return;
    *div = (uint8_t)nd;
    *end = (uint8_t)(lo + n - 1);
}
void s4_duplicate(int s, int t)                /* the steps again after themselves (up to 128) */
{
    s4_seq_t *q = &s4[s];
    int lo, hi, n, i, l;
    uint8_t *end = q->mode == M_MULTI ? &q->d.multi.t[t & 7].end : &q->end;
    range_of(s, t, &lo, &hi);
    n = hi - lo + 1;
    if (hi + 1 > 127)
        return;
    if (hi + n > 127)
        n = 127 - hi;
    if (q->mode == M_MONO || q->mode == M_MULTI || q->mode == M_STOCH) {
        s4_step_t *st = note_steps(s, t);
        for (i = 0; i < n; i++)
            st[hi + 1 + i] = st[lo + i];
    } else if (q->mode == M_CHORD) {
        for (i = 0; i < n; i++)
            q->d.chord[hi + 1 + i] = q->d.chord[lo + i];
    } else if (q->mode == M_POLY) {
        int cnt = q->d.poly.n;
        for (i = 0; i < cnt; i++)
            if (q->d.poly.note[i].step >= lo && q->d.poly.note[i].step < lo + n) {
                s4_pnote_t pn = q->d.poly.note[i];
                pn.step = (uint8_t)(pn.step + (hi + 1 - lo));
                s4_poly_add(s, &pn);
            }
    } else
        return;
    for (l = 0; l < S4_NMOD; l++)
        if (q->ml[l].linked)
            for (i = 0; i < n; i++)
                q->mod[l][hi + 1 + i] = q->mod[l][lo + i];
    *end = (uint8_t)(hi + n);
}

void s4_set_scale(int s, int sc, int quantize)
{
    s4_seq_t *q = &s4[s];
    int i, old = q->scale;
    if (sc == old)
        return;
    if (quantize && old < S4_HARMO && sc < S4_HARMO) {   /* the notes kept as near as the new scale allows */
        int n;
        switch (q->mode) {
        case M_MONO:
            for (i = 0; i < S4_NSTEP; i++)
                if (q->d.mono[i].vel) {
                    n = seq_base(s) + deg_semis(old, q->d.mono[i].deg);
                    q->scale = (uint8_t)sc;
                    q->d.mono[i].deg = (int8_t)s4_deg_of(s, n);
                    q->scale = (uint8_t)old;
                }
            break;
        case M_CHORD:
            for (i = 0; i < S4_NSTEP; i++)
                if (q->d.chord[i].vel) {
                    n = seq_base(s) + deg_semis(old, q->d.chord[i].deg);
                    q->scale = (uint8_t)sc;
                    q->d.chord[i].deg = (int8_t)s4_deg_of(s, n);
                    q->scale = (uint8_t)old;
                }
            break;
        case M_POLY:
            for (i = 0; i < q->d.poly.n; i++) {
                n = seq_base(s) + deg_semis(old, q->d.poly.note[i].deg);
                q->scale = (uint8_t)sc;
                q->d.poly.note[i].deg = (int8_t)s4_deg_of(s, n);
                q->scale = (uint8_t)old;
            }
            break;
        default:
            break;
        }
    }
    q->scale = (uint8_t)sc;
}

/* ---------------------------------------------------------------------------- the generators --- */
void s4_euclid(int s, int t)
{
    s4_seq_t *q = &s4[s];
    s4_trk_t *k = &q->d.multi.t[t & 7];
    int i, len = k->eu_len ? k->eu_len : 16;
    s4_step_t *st;
    if (q->mode != M_MULTI)
        return;
    st = q->d.multi.s[t & 7];
    if (k->init + len - 1 > 127)
        len = 128 - k->init;
    k->end = (uint8_t)(k->init + len - 1);
    for (i = 0; i < len; i++) {
        s4_step_t *x = &st[k->init + i];
        int hit = euclid_hit(len, k->eu_puls, k->eu_rot, i) ^ (k->eu_inv ? 1 : 0);
        if (is_locked(x->cond) && x->vel)
            continue;
        if (hit) {
            if (!x->vel) {
                x->deg = 0;
                x->oct = 0;
                x->gate = q->gdef ? q->gdef : 50;
                x->cond = 100;
                x->rtg = 0;
                x->ofs = 0;
                x->glide = 0;
            }
            x->vel = q->vdef ? q->vdef : 100;
        } else
            x->vel = 0;
    }
}

static void humanize(s4_step_t *x, int h)
{
    if (!x->vel || is_locked(x->cond))
        return;
    x->vel = (uint8_t)clampi(x->vel + rnd_n(h + 1) - h / 2, 1, 127);
    if (x->gate <= 100)
        x->gate = (uint8_t)clampi(x->gate + rnd_n(h / 2 + 1) - h / 4, 1, 100);
    x->ofs = (int8_t)clampi(rnd_n(h / 4 + 1) - h / 8, -45, 45);
}

static void gen_notes(int s, int t, s4_step_t *st, int lo, int hi, int drum)
{
    s4_seq_t *q = &s4[s];
    int range = q->rg_range < 0 ? -q->rg_range : q->rg_range, bip = q->rg_range < 0, i;
    int motif[8], ml = 2 + (100 - clampi(q->rg_rand, 0, 100)) / 15, prev_on = 0;
    for (i = 0; i < ml && i < 8; i++)
        motif[i] = q->rg_bias + (bip ? rnd_n(2 * range + 1) - range : rnd_n(range + 1));
    for (i = lo; i <= hi; i++) {
        s4_step_t *x = &st[i];
        if (is_locked(x->cond) && x->vel)
            continue;
        if (!chance(q->rg_dens)) {
            x->vel = 0;
            prev_on = 0;
            continue;
        }
        if (!drum) {
            if (chance(q->rg_rand))
                x->deg = (int8_t)(q->rg_bias + (bip ? rnd_n(2 * range + 1) - range : rnd_n(range + 1)));
            else
                x->deg = (int8_t)motif[(i - lo) % (ml < 8 ? ml : 8)];
        } else if (!x->vel)
            x->deg = 0;
        x->oct = 0;
        x->vel = (uint8_t)clampi((q->vdef ? q->vdef : 100) - 10 + rnd_n(21), 1, 127);
        x->gate = q->gdef ? q->gdef : 75;
        x->cond = 100;
        x->rtg = 0;
        x->ofs = 0;
        if (q->rg_human)
            humanize(x, q->rg_human);
        if (!drum && prev_on && q->rg_ties && chance(q->rg_ties)) {   /* tied to the step before */
            st[i - 1].gate = S4_TIE;
            x->deg = st[i - 1].deg;
        }
        prev_on = 1;
    }
    (void)t;
}
static void shuffle_steps(s4_step_t *st, int lo, int hi, int pitch_only)
{
    int idx[128], n = 0, i;
    for (i = lo; i <= hi; i++)
        if (st[i].vel && !is_locked(st[i].cond))
            idx[n++] = i;
    for (i = n - 1; i > 0; i--) {
        int j = rnd_n(i + 1);
        if (pitch_only) {
            int8_t d = st[idx[i]].deg, o = st[idx[i]].oct;
            st[idx[i]].deg = st[idx[j]].deg;
            st[idx[i]].oct = st[idx[j]].oct;
            st[idx[j]].deg = d;
            st[idx[j]].oct = o;
        } else {
            s4_step_t x = st[idx[i]];
            st[idx[i]] = st[idx[j]];
            st[idx[j]] = x;
        }
        if (st[idx[i]].gate >= S4_TIE)
            st[idx[i]].gate = 100;
        if (st[idx[j]].gate >= S4_TIE)
            st[idx[j]].gate = 100;
    }
}
void s4_generate(int s, int trk)
{
    s4_seq_t *q = &s4[s];
    int lo, hi, i, t;
    s4_undo_push(s);
    if (q->rg_scale) {                         /* a scale and a root too */
        static const uint8_t SC[] = {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12};
        q->scale = SC[rnd_n((int)sizeof SC)];
        q->root = (uint8_t)rnd_n(12);
    }
    switch (q->mode) {
    case M_MONO:
    case M_MULTI: {
        int t0 = q->mode == M_MULTI ? (trk < 0 ? 0 : trk) : 0, t1 = q->mode == M_MULTI ? (trk < 0 ? 7 : trk) : 0;
        for (t = t0; t <= t1; t++) {
            s4_step_t *st = note_steps(s, t);
            range_of(s, t, &lo, &hi);
            switch (q->rg_dens) {
            case 101:
                for (i = lo; i <= hi; i++)
                    humanize(&st[i], q->rg_human ? q->rg_human : 30);
                break;
            case 102: shuffle_steps(st, lo, hi, 0); break;
            case 103: shuffle_steps(st, lo, hi, 1); break;
            case 104:                          /* inverted: back to front */
                for (i = 0; i < (hi - lo + 1) / 2; i++) {
                    s4_step_t x = st[lo + i];
                    st[lo + i] = st[hi - i];
                    st[hi - i] = x;
                }
                break;
            default:
                gen_notes(s, t, st, lo, hi, q->mode == M_MULTI && trk < 0);
                break;
            }
        }
        break;
    }
    case M_CHORD:
        range_of(s, 0, &lo, &hi);
        for (i = lo; i <= hi; i++) {
            s4_cstep_t *c = &q->d.chord[i];
            if (is_locked(c->cond) && c->vel)
                continue;
            if (!chance(q->rg_dens)) {
                c->vel = 0;
                continue;
            }
            c->deg = (int8_t)(q->rg_bias + rnd_n(7));
            c->type = (uint8_t)(chance(q->rg_rand) ? rnd_n(6) : 0);
            c->voi = (uint8_t)rnd_n(3);
            c->spread = 0;
            c->vel = q->vdef ? q->vdef : 100;
            c->gate = q->gdef ? q->gdef : 75;
            c->cond = 100;
            c->rtg = 0;
            c->ofs = 0;
            c->oct = 0;
        }
        break;
    case M_POLY:
        range_of(s, 0, &lo, &hi);
        for (i = q->d.poly.n - 1; i >= 0; i--)
            if (q->d.poly.note[i].step >= lo && q->d.poly.note[i].step <= hi && !is_locked(q->d.poly.note[i].cond))
                s4_poly_del(s, i);
        for (i = lo; i <= hi; i++) {
            int k, nn;
            if (!chance(q->rg_dens))
                continue;
            nn = 1 + rnd_n(3);
            for (k = 0; k < nn; k++) {
                s4_pnote_t pn;
                int range = q->rg_range < 0 ? -q->rg_range : q->rg_range;
                pn.step = (uint8_t)i;
                pn.deg = (int8_t)(q->rg_bias + (q->rg_range < 0 ? rnd_n(2 * range + 1) - range : rnd_n(range + 1)));
                pn.vel = q->vdef ? q->vdef : 100;
                pn.gate = q->gdef ? q->gdef : 75;
                pn.oct = 0;
                pn.cond = 100;
                pn.rtg = 0;
                pn.ofs = 0;
                if (s4_poly_find(s, i, pn.deg) < 0)
                    s4_poly_add(s, &pn);
            }
        }
        break;
    case M_STOCH:
        for (i = 0; i < 12; i++)
            q->d.stoch.prob[i] = (uint8_t)(chance(q->rg_dens) ? rnd_n(8) : 0);
        q->d.stoch.prob[0] = (uint8_t)(4 + rnd_n(4));
        break;
    default: {                                 /* Matriceal: every lane of the track, by its own amount */
        int tt = trk < 0 ? 0 : trk & 3, l, k;
        s4_mxtrk_t *x = &q->d.mx[tt];
        for (l = 0; l < S4_NLANE; l++)
            for (k = 0; k < 16; k++)
                if (chance(x->lane[l].rand)) {
                    switch (l) {
                    case L_TRIG: x->v[l][k] = (int8_t)chance(q->rg_dens); break;
                    case L_NOTE: x->v[l][k] = (int8_t)(rnd_n(15) - 7); break;
                    case L_INTV: x->v[l][k] = (int8_t)(rnd_n(5) - 2); break;
                    case L_VEL: x->v[l][k] = (int8_t)(60 + rnd_n(67)); break;
                    case L_OCT: x->v[l][k] = (int8_t)(rnd_n(3) - 1); break;
                    case L_GATE: x->v[l][k] = (int8_t)(10 + rnd_n(91)); break;
                    case L_PROB: x->v[l][k] = (int8_t)(40 + rnd_n(61)); break;
                    case L_RTG: x->v[l][k] = (int8_t)(1 + (chance(20) ? rnd_n(4) : 0)); break;
                    case L_PULS: x->v[l][k] = (int8_t)(1 + (chance(15) ? rnd_n(3) : 0)); break;
                    default: x->v[l][k] = (int8_t)(chance(20) ? rnd_n(60) : 0); break;
                    }
                }
        break;
    }
    }
}
void s4_stoch_lock(int s)
{
    s4[s].d.stoch.pitch = 0;
    s4[s].d.stoch.rhythm = 0;
}

/* --------------------------------------------------------------------------- a pattern as bytes --- */
#define PK_MAGIC0 'S'
#define PK_MAGIC1 '4'
#define PK_VER 1
#define PK_SET ((uint32_t)offsetof(s4_seq_t, mod))   /* the settings' bytes */

typedef struct {
    uint8_t *b;
    uint32_t n, max;
    int bad;
} wr_t;
static void w1(wr_t *w, uint32_t v)
{
    if (w->n < w->max)
        w->b[w->n++] = (uint8_t)v;
    else
        w->bad = 1;
}
static void wn(wr_t *w, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    while (n--)
        w1(w, *s++);
}
static void w_rle(wr_t *w, const uint8_t *v, uint32_t n)   /* (count, value) pairs */
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t k = 1;
        while (i + k < n && k < 255u && v[i + k] == v[i])
            k++;
        w1(w, k);
        w1(w, v[i]);
        i += k;
    }
}
uint32_t s4_pack(const s4_seq_t *q, uint8_t *b, uint32_t max)
{
    wr_t w;
    int i, l, t;
    w.b = b;
    w.n = 0;
    w.max = max;
    w.bad = 0;
    w1(&w, PK_MAGIC0);
    w1(&w, PK_MAGIC1);
    w1(&w, PK_VER);
    w1(&w, q->mode);
    w1(&w, PK_SET & 0xFF);
    w1(&w, PK_SET >> 8);
    wn(&w, q, PK_SET);
    for (l = 0; l < S4_NMOD; l++) {            /* the lanes: empty ones a zero */
        int any = 0;
        for (i = 0; i < S4_NSTEP; i++)
            any |= q->mod[l][i];
        w1(&w, (uint32_t)any != 0);
        if (any)
            w_rle(&w, q->mod[l], S4_NSTEP);
    }
    switch (q->mode) {
    case M_MONO:
    case M_STOCH: {
        const s4_step_t *st = q->mode == M_MONO ? q->d.mono : q->d.stoch.buf;
        int n = 0;
        if (q->mode == M_STOCH)
            wn(&w, &q->d.stoch, (uint32_t)((const uint8_t *)q->d.stoch.buf - (const uint8_t *)&q->d.stoch));
        for (i = 0; i < S4_NSTEP; i++)
            n += st[i].vel != 0 || st[i].gate != 0;
        w1(&w, (uint32_t)n);
        for (i = 0; i < S4_NSTEP; i++)
            if (st[i].vel || st[i].gate) {
                w1(&w, (uint32_t)i);
                wn(&w, &st[i], sizeof st[i]);
            }
        break;
    }
    case M_POLY:
        w1(&w, q->d.poly.n & 0xFF);
        w1(&w, q->d.poly.n >> 8);
        wn(&w, q->d.poly.note, (uint32_t)q->d.poly.n * sizeof(s4_pnote_t));
        w_rle(&w, q->d.poly.glide, S4_NSTEP);
        break;
    case M_CHORD: {
        int n = 0;
        for (i = 0; i < S4_NSTEP; i++)
            n += q->d.chord[i].vel != 0;
        w1(&w, (uint32_t)n);
        for (i = 0; i < S4_NSTEP; i++)
            if (q->d.chord[i].vel) {
                w1(&w, (uint32_t)i);
                wn(&w, &q->d.chord[i], sizeof q->d.chord[i]);
            }
        break;
    }
    case M_MULTI:
        wn(&w, q->d.multi.t, sizeof q->d.multi.t);
        for (t = 0; t < S4_NTRK; t++) {
            int n = 0;
            for (i = 0; i < S4_NSTEP; i++)
                n += q->d.multi.s[t][i].vel != 0;
            w1(&w, (uint32_t)n);
            for (i = 0; i < S4_NSTEP; i++)
                if (q->d.multi.s[t][i].vel) {
                    w1(&w, (uint32_t)i);
                    wn(&w, &q->d.multi.s[t][i], sizeof(s4_step_t));
                }
        }
        break;
    default:
        wn(&w, q->d.mx, sizeof q->d.mx);
        break;
    }
    return w.bad ? 0u : w.n;
}

typedef struct {
    const uint8_t *b;
    uint32_t n, i;
    int bad;
} rd_t;
static uint32_t r1(rd_t *r)
{
    if (r->i < r->n)
        return r->b[r->i++];
    r->bad = 1;
    return 0;
}
static void rn(rd_t *r, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n--)
        *d++ = (uint8_t)r1(r);
}
static void r_rle(rd_t *r, uint8_t *v, uint32_t n)
{
    uint32_t i = 0;
    while (i < n && !r->bad) {
        uint32_t k = r1(r), x = r1(r);
        while (k-- && i < n)
            v[i++] = (uint8_t)x;
    }
}
int s4_unpack(s4_seq_t *q, const uint8_t *b, uint32_t n)
{
    rd_t r;
    uint32_t set;
    int i, l, t, mode, cnt;
    r.b = b;
    r.n = n;
    r.i = 0;
    r.bad = 0;
    if (r1(&r) != PK_MAGIC0 || r1(&r) != PK_MAGIC1 || r1(&r) != PK_VER)
        return -1;
    mode = (int)r1(&r);
    set = r1(&r);
    set |= r1(&r) << 8;
    if (set != PK_SET || mode >= M_N)
        return -2;
    zero(q, sizeof *q);
    rn(&r, q, PK_SET);
    q->mode = (uint8_t)mode;
    for (l = 0; l < S4_NMOD; l++)
        if (r1(&r))
            r_rle(&r, q->mod[l], S4_NSTEP);
    switch (mode) {
    case M_MONO:
    case M_STOCH: {
        s4_step_t *st = mode == M_MONO ? q->d.mono : q->d.stoch.buf;
        if (mode == M_STOCH)
            rn(&r, &q->d.stoch, (uint32_t)((const uint8_t *)q->d.stoch.buf - (const uint8_t *)&q->d.stoch));
        cnt = (int)r1(&r);
        for (i = 0; i < cnt && !r.bad; i++) {
            uint32_t k = r1(&r) & 127u;
            rn(&r, &st[k], sizeof st[k]);
        }
        break;
    }
    case M_POLY:
        q->d.poly.n = (uint16_t)r1(&r);
        q->d.poly.n |= (uint16_t)(r1(&r) << 8);
        if (q->d.poly.n > S4_NPN)
            return -3;
        rn(&r, q->d.poly.note, (uint32_t)q->d.poly.n * sizeof(s4_pnote_t));
        r_rle(&r, q->d.poly.glide, S4_NSTEP);
        break;
    case M_CHORD:
        cnt = (int)r1(&r);
        for (i = 0; i < cnt && !r.bad; i++) {
            uint32_t k = r1(&r) & 127u;
            rn(&r, &q->d.chord[k], sizeof q->d.chord[k]);
        }
        break;
    case M_MULTI:
        rn(&r, q->d.multi.t, sizeof q->d.multi.t);
        for (t = 0; t < S4_NTRK; t++) {
            cnt = (int)r1(&r);
            for (i = 0; i < cnt && !r.bad; i++) {
                uint32_t k = r1(&r) & 127u;
                rn(&r, &q->d.multi.s[t][k], sizeof(s4_step_t));
            }
        }
        break;
    default:
        rn(&r, q->d.mx, sizeof q->d.mx);
        break;
    }
    return r.bad ? -4 : 0;
}
