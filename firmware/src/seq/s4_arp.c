/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: the arpeggiators and the keys played live. Part of seq4.c.
 *   the keyboard's arpeggiator   plays the keys held (or latched: hold)
 *   the sequence's (Poly, Chord) plays each step's notes for as long as the step lasts
 * Both: a mode (up, down, up-down, ends twice, converging, diverging, both, pinky, thumb, random, random
 * once, as played), octaves up or down, a gate, a division, a groove of accents, and a Euclidean rhythm
 * (length, pulses, rotation; a pulse plays so many notes in a row). The roller repeats the keys held at its
 * division. A key with none of these plays straight out on its sequencer's (track's) port and channel. */

static const int8_t GROOVE[16] = {24, -12, 4, -18, 14, -10, 6, -20, 22, -12, 2, -16, 12, -8, 8, -22};

static void arp_build(arp_t *a, const s4_arp_t *cfg, uint8_t *seq, uint8_t *nseq, uint8_t *list, uint8_t *lvel)
{
    uint8_t base[16], bv[16];
    int n = a->nin, k, o, m = 0, oc = cfg->oct, i, j;
    if (n > 16)
        n = 16;
    for (k = 0; k < n; k++) {
        base[k] = a->in[k];
        bv[k] = a->inv[k];
    }
    if (cfg->mode != A_ORDER)                  /* sorted, low to high (but for as-played) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && base[j - 1] > base[j]; j--) {
                uint8_t x = base[j];
                base[j] = base[j - 1];
                base[j - 1] = x;
                x = bv[j];
                bv[j] = bv[j - 1];
                bv[j - 1] = x;
            }
    for (o = oc < 0 ? oc : 0; o <= (oc > 0 ? oc : 0); o++)   /* octaves added up (or down) */
        for (k = 0; k < n && m < 48; k++) {
            int v = base[k] + 12 * o;
            if (v < 0 || v > 127)
                continue;
            list[m] = (uint8_t)v;
            lvel[m] = bv[k];
            m++;
        }
    n = m;
    m = 0;
    switch (cfg->mode) {
    case A_DOWN:
        for (k = n - 1; k >= 0; k--)
            seq[m++] = (uint8_t)k;
        break;
    case A_UPDN:
        for (k = 0; k < n; k++)
            seq[m++] = (uint8_t)k;
        for (k = n - 2; k > 0; k--)
            seq[m++] = (uint8_t)k;
        break;
    case A_2U2D:                               /* the ends twice */
        for (k = 0; k < n; k++)
            seq[m++] = (uint8_t)k;
        for (k = n - 1; k >= 0 && m < 96; k--)
            seq[m++] = (uint8_t)k;
        break;
    case A_ALTIN:                              /* converging: low, high, next low, next high .. */
        for (i = 0, j = n - 1; i <= j; i++, j--) {
            seq[m++] = (uint8_t)i;
            if (j != i)
                seq[m++] = (uint8_t)j;
        }
        break;
    case A_ALTOUT:                             /* diverging: from the middle out */
        for (i = (n - 1) / 2, j = i + 1; i >= 0 || j < n; i--, j++) {
            if (i >= 0)
                seq[m++] = (uint8_t)i;
            if (j < n)
                seq[m++] = (uint8_t)j;
        }
        break;
    case A_ALTIO:
        for (i = 0, j = n - 1; i <= j; i++, j--) {
            seq[m++] = (uint8_t)i;
            if (j != i)
                seq[m++] = (uint8_t)j;
        }
        for (i = (n - 1) / 2, j = i + 1; (i >= 0 || j < n) && m < 96; i--, j++) {
            if (i >= 0)
                seq[m++] = (uint8_t)i;
            if (j < n)
                seq[m++] = (uint8_t)j;
        }
        break;
    case A_PINKY:                              /* each note, then the top */
        for (k = 0; k < n - 1 && m < 94; k++) {
            seq[m++] = (uint8_t)k;
            seq[m++] = (uint8_t)(n - 1);
        }
        if (n == 1)
            seq[m++] = 0;
        break;
    case A_THUMB:                              /* each note, then the bottom */
        for (k = 1; k < n && m < 94; k++) {
            seq[m++] = (uint8_t)k;
            seq[m++] = 0;
        }
        if (n == 1)
            seq[m++] = 0;
        break;
    case A_RAND1: {                            /* a random order, kept */
        for (k = 0; k < n; k++)
            seq[k] = (uint8_t)k;
        for (k = n - 1; k > 0; k--) {
            int r = rnd_n(k + 1);
            uint8_t x = seq[k];
            seq[k] = seq[r];
            seq[r] = x;
        }
        m = n;
        break;
    }
    default:                                   /* up, random (picked as it plays), as played */
        for (k = 0; k < n; k++)
            seq[m++] = (uint8_t)k;
        break;
    }
    *nseq = (uint8_t)(m ? m : 1);
}

typedef struct {
    uint8_t seq[96], nseq, list[48], lvel[48], built;
    uint8_t pin[16], pinv[16], pnin;           /* the sequence arp's next notes (from the next step) */
    uint32_t pstart, puntil;
} arpx_t;
static arpx_t ax[S4_NSEQ][2];

static void arp_dirty(int s, int which) { ax[s][which].built = 0; }

static void arp_one(int s, int which)
{
    rt_t *r = &rt[s];
    arp_t *a = which ? &r->sarp : &r->karp;
    const s4_arp_t *cfg = which ? &s4[s].arp : &s4[s].karp;
    arpx_t *x = &ax[s][which];
    int port, ch, guard = 16, d;
    uint32_t u;
    if (which && x->pnin && (int32_t)(r->lpos - x->pstart) >= 0) {   /* the next step's notes take over */
        int k;
        for (k = 0; k < x->pnin; k++) {
            a->in[k] = x->pin[k];
            a->inv[k] = x->pinv[k];
        }
        a->nin = x->pnin;
        a->until = x->puntil;
        a->next = x->pstart;
        x->pnin = 0;
        x->built = 0;
        if (s4g.arp_reset)
            a->idx = 0;
    }
    if (which && a->nin && (int32_t)(r->lpos - a->until) >= 0)
        a->nin = 0;
    if (!cfg->mode || !a->nin) {
        a->next = r->lpos;
        return;
    }
    if (!x->built) {
        arp_build(a, cfg, x->seq, &x->nseq, x->list, x->lvel);
        x->built = 1;
    }
    d = clampi(cfg->div + dm[s][0][which ? S4_D_ARP_DIV : S4_D_KARP_DIV], DV_1BAR, DV_64);
    u = s4_div_units(d);
    trk_port(s, a->trk, &port, &ch);
    while (guard-- && (int32_t)(r->lpos - a->next) >= 0) {
        int play = 1;
        if (cfg->len && cfg->puls < cfg->len) {   /* the Euclidean rhythm: a pulse, then its repeats */
            if (a->rptl)
                a->rptl--;
            else {
                play = euclid_hit(cfg->len, cfg->puls, cfg->rot, a->eu);
                if (play)
                    a->rptl = (uint8_t)(cfg->rpt > 1 ? cfg->rpt - 1 : 0);
            }
            a->eu = (uint16_t)((a->eu + 1) % cfg->len);
        }
        if (play && x->nseq) {
            int i = cfg->mode == A_RAND ? rnd_n(x->nseq) : x->seq[a->idx % x->nseq];
            int note = x->list[i % 48], vel = which || s4g.karp_vel ? x->lvel[i % 48] : (s4[s].vdef ? s4[s].vdef : 100);
            int acc = cfg->accent * GROOVE[a->pulse & 15] / 100;
            uint8_t tk = tok_new();
            uint32_t dur = u * (uint32_t)clampi(cfg->gate ? cfg->gate : 50, 5, 100) / 100u;
            vel = clampi(vel + acc, 1, 127);
            q_add(s, a->next, E_ON, port, ch, note, vel, tk, a->trk);
            q_add(s, a->next + dur, E_OFF, port, ch, note, 0, tk, a->trk);
            a->idx = (uint8_t)((a->idx + 1) % x->nseq);
            s4p.lit[s][a->trk & 7]++;
        }
        a->pulse++;
        a->next += u;
    }
}

static void arp_feed(int s, const uint8_t *notes, const uint8_t *vels, int n, uint32_t T, uint32_t dur)
{
    arpx_t *x = &ax[s][1];
    int k;
    for (k = 0; k < n && k < 16; k++) {
        x->pin[k] = notes[k];
        x->pinv[k] = vels[k];
    }
    x->pnin = (uint8_t)(n < 16 ? n : 16);
    x->pstart = T;
    x->puntil = T + (dur ? dur : 1u);
    rt[s].sarp.trk = 0;
}

/* the roller: the keys held, again at its division */
static void roll_run(int s)
{
    rt_t *r = &rt[s];
    int t, k, port, ch, guard = 8;
    uint32_t u;
    if (!r->roll) {
        r->roll_next = r->lpos;
        return;
    }
    u = s4_div_units(r->roll);
    while (guard-- && (int32_t)(r->lpos - r->roll_next) >= 0) {
        for (t = 0; t < S4_NTRK; t++) {
            trk_port(s, t, &port, &ch);
            for (k = 0; k < r->nlive[t]; k++) {
                uint8_t tk = tok_new();
                q_add(s, r->roll_next, E_ON, port, ch, r->live[t][k], r->livev[t][k], tk, t);
                q_add(s, r->roll_next + u / 2u, E_OFF, port, ch, r->live[t][k], 0, tk, t);
            }
        }
        r->roll_next += u;
    }
}

static void arp_run(int s)
{
    arp_one(s, 0);
    if (s4[s].mode == M_POLY || s4[s].mode == M_CHORD)
        arp_one(s, 1);
    roll_run(s);
}

/* a key (or a note coming in through this sequencer): on (vel > 0) or off */
static void leader_live(int s)                 /* a Chord / Poly leader played live: its followers follow */
{
    rt_t *r = &rt[s];
    int k, n = 0;
    uint8_t c[8];
    if (s4[s].mode != M_CHORD && s4[s].mode != M_POLY)
        return;
    for (k = 0; k < r->nlive[0] && n < 8; k++)
        c[n++] = r->live[0][k];
    if (!n)
        return;
    sort_u8(c, n);
    for (k = 0; k < n; k++)
        s4p.chord[s][k] = c[k];
    s4p.chord_n[s] = (uint8_t)n;
}
static void live_note(int s, int trk, int note, int vel)
{
    rt_t *r = &rt[s];
    arp_t *a = &r->karp;
    const s4_arp_t *cfg = &s4[s].karp;
    int port, ch, k, play = vel >= 0x80 ? 0 : 1;     /* (vel | 0x80: use it, don't play it: thru took it) */
    trk &= 7;
    vel &= 0x7F;
    if (note < 0 || note > 127)
        return;
    trk_port(s, trk, &port, &ch);
    rec_note(s, trk, note, vel);
    if (vel) {                                 /* the keys held */
        for (k = 0; k < r->nlive[trk]; k++)
            if (r->live[trk][k] == note)
                break;
        if (k == r->nlive[trk] && k < 16) {
            r->live[trk][k] = (uint8_t)note;
            r->livev[trk][k] = (uint8_t)vel;
            r->nlive[trk]++;
        }
    } else {
        for (k = 0; k < r->nlive[trk]; k++)
            if (r->live[trk][k] == note) {
                for (; k < r->nlive[trk] - 1; k++) {
                    r->live[trk][k] = r->live[trk][k + 1];
                    r->livev[trk][k] = r->livev[trk][k + 1];
                }
                r->nlive[trk]--;
                break;
            }
    }
    leader_live(s);
    if (cfg->mode || cfg->hold) {              /* the arpeggiator's (or hold's) */
        a->trk = (uint8_t)trk;
        if (vel) {
            if (cfg->hold && !s4g.latch && a->latched) {   /* a new hand after letting go: a new set */
                a->nin = 0;
                a->latched = 0;
            }
            for (k = 0; k < a->nin; k++)
                if (a->in[k] == note)
                    break;
            if (k == a->nin && k < 16) {
                a->in[k] = (uint8_t)note;
                a->inv[k] = (uint8_t)vel;
                a->nin++;
                if (a->nin == 1) {
                    a->next = r->lpos;
                    a->idx = 0;
                    a->eu = 0;
                    a->rptl = 0;
                }
                if (s4g.arp_reset)
                    a->idx = 0;
            }
            if (!cfg->mode && play)            /* hold alone: a drone */
                n_on(port, ch, note, vel, 1);
        } else if (!cfg->hold) {
            for (k = 0; k < a->nin; k++)
                if (a->in[k] == note) {
                    for (; k < a->nin - 1; k++) {
                        a->in[k] = a->in[k + 1];
                        a->inv[k] = a->inv[k + 1];
                    }
                    a->nin--;
                    break;
                }
        } else if (!r->nlive[trk]) {
            a->latched = 1;
        }
        arp_dirty(s, 0);
        return;
    }
    if (r->roll)                               /* the roller plays them */
        return;
    if (!play)
        return;
    if (vel)
        n_on(port, ch, note, vel, 1);
    else
        n_off(port, ch, note, 1);
}
void s4_arp_clear(int s)                       /* hold let go: the held notes off */
{
    rt[s].karp.nin = 0;
    arp_dirty(s, 0);
}
