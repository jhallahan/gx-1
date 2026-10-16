/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: what moves the settings. Part of seq4.c.
 *   LFOs        two a sequencer: a wave, a rate (synced, 16 bars .. 1/32, or free, 0.05 .. 200 Hz), an
 *               amount; each moves one internal destination and sends one MIDI value (a CC, pitch bend,
 *               aftertouch, a program)
 *   mod lanes   a value a step (4 lanes; Multitrack one a track): sent as MIDI as the steps pass (smoothed
 *               if asked), and moving up to two internal destinations
 *   mappings    something coming in (a CC, a note) moves internal destinations, changes a setting, or is
 *               sent on
 * Every source adds into dm[sequencer][track][destination], read when a step is played. */

static const char *const DEST_NAME[S4_D_N] = {
    "Off", "Velocity", "Pitch", "Octave", "Gate", "Division", "Retrig", "Probability", "Glide",
    "Kb Arp Div", "Kb Arp Oct", "Kb Arp Type", "LFO1 Wave", "LFO1 Rate", "LFO1 Amt", "LFO2 Wave", "LFO2 Rate",
    "LFO2 Amt", "Arp Div", "Arp Oct", "Arp Type", "Chord Root", "Chord Type", "Voicing", "Strum", "Spread",
    "Euclid Puls", "Euclid Rot", "Kick Dens", "Snare Dens", "Hats Dens", "Drums X", "Drums Y", "Chaos",
    "Note Prob", "Pitch Lock", "Rhythm Lock", "Longer", "Shorter", "Oct Up", "Oct Down", "Interval"};
const char *s4_dest_name(int d) { return DEST_NAME[(unsigned)d < S4_D_N ? d : 0]; }
int s4_dest_ok(int mode, int d)
{
    if (d < S4_D_ARP_DIV)
        return 1;
    if (d <= S4_D_ARP_TYPE)
        return mode == M_POLY || mode == M_CHORD;
    if (d <= S4_D_SPREAD)
        return mode == M_CHORD;
    if (d <= S4_D_DG_CHAOS)
        return mode == M_MULTI;
    if (d <= S4_D_ST_DN)
        return mode == M_STOCH;
    return mode == M_MATRIX;
}
static const char *const PARA_NAME[S4_P_N] = {
    "Off", "Seq On", "Seq Mute", "Division", "Direction", "Scale", "Root", "Swing", "Loop", "Loop Off",
    "Time Bend", "Nudge", "Init", "End", "Lane On", "Lane Ofs", "Rnd Vel", "Rnd Oct", "Rnd Trig", "Rnd Retrig",
    "Tempo", "Fill", "Reset", "Generate", "Reload", "Tap", "Record"};
const char *s4_para_name(int p) { return PARA_NAME[(unsigned)p < S4_P_N ? p : 0]; }

/* ------------------------------------------------------------------------------------- LFOs --- */
#define LFO_SYNC 10                            /* rates 0..9 synced, 10..59 free */
static const uint32_t LFO_SYNC_U[LFO_SYNC] = {16u * 15360u, 8u * 15360u, 4u * 15360u, 2u * 15360u, 15360u, 7680u,
                                              3840u, 1920u, 960u, 480u};
static float lfo_hz(int r) { return 0.05f * fm_exp2f((float)(r - LFO_SYNC) * (11.97f / 49.0f)); }   /* .. 200 Hz */

static float lfo_wave(int w, float ph, float sh, float sh2)
{
    switch (w) {
    case W_SINE: return fm_sin_turns(ph);
    case W_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case W_SAWUP: return 2.0f * ph - 1.0f;
    case W_SAWDN: return 1.0f - 2.0f * ph;
    case W_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case W_PULSE: return ph < 0.25f ? 1.0f : -1.0f;
    case W_SH: return sh;
    case W_SMOOTH: {                           /* the last random value to the next, eased */
        float e = ph * ph * (3.0f - 2.0f * ph);
        return sh2 + (sh - sh2) * e;
    }
    case W_EXPUP: return 2.0f * ph * ph * ph - 1.0f;
    case W_EXPDN: return 2.0f * (1.0f - ph) * (1.0f - ph) * (1.0f - ph) - 1.0f;
    default: return (float)((int)(ph * 4.0f)) * (2.0f / 3.0f) - 1.0f;   /* stairs: four */
    }
}

static int lfo_track_on(int s, int t, int i)   /* Multitrack: does LFO i move track t */
{
    return s4[s].mode != M_MULTI || (s4[s].d.multi.t[t].lfo_on >> i & 1u);
}

static void trk_port(int s, int t, int *port, int *ch)
{
    if (s4[s].mode == M_MULTI) {
        *port = s4[s].d.multi.t[t & 7].port & 3;
        *ch = s4[s].d.multi.t[t & 7].ch & 15;
    } else if (s4[s].mode == M_MATRIX) {
        *port = s4[s].d.mx[t & 3].port & 3;
        *ch = s4[s].d.mx[t & 3].ch & 15;
    } else {
        *port = s4[s].port & 3;
        *ch = s4[s].ch & 15;
    }
}

static void send_x(int s, int t, int type, int num, int v7, int v14, uint8_t *last)
{
    int port, ch;
    if (*last == (uint8_t)v7 && type != X_PB)
        return;
    *last = (uint8_t)v7;
    trk_port(s, t, &port, &ch);
    switch (type) {
    case X_CC: out3(port, 0xB0 | ch, num, v7); break;
    case X_PB: out3(port, 0xE0 | ch, v14 & 0x7F, v14 >> 7); break;
    case X_AT: out3(port, 0xD0 | ch, v7, 0); break;
    case X_PC: out3(port, 0xC0 | ch, v7, 0); break;
    default: break;
    }
}

/* a mod lane's step now (linked: the sequence's, or its track's in Multitrack; else its own clock) */
static int lane_step(int s, int l)
{
    if (!s4[s].ml[l].linked)
        return rt[s].mc[l].pos;
    if (s4[s].mode == M_MULTI)
        return rt[s].c[l & 7].pos;
    if (s4[s].mode == M_MATRIX)
        return rt[s].lc[l & 3][L_TRIG].pos;
    return rt[s].c[0].pos;
}
static int lane_track(int s, int l) { return s4[s].mode == M_MULTI ? (l & 7) : s4[s].mode == M_MATRIX ? (l & 3) : -1; }
static int lanes_in_use(int s) { return s4[s].mode == M_MULTI ? 8 : 4; }

static void dm_add(int s, int trk, int d, int v)
{
    int t;
    if (!d || d >= S4_D_N)
        return;
    if (trk >= 0) {
        dm[s][trk][d] = (int16_t)clampi(dm[s][trk][d] + v, -32000, 32000);
        return;
    }
    for (t = 0; t < S4_NTRK; t++)
        dm[s][t][d] = (int16_t)clampi(dm[s][t][d] + v, -32000, 32000);
}

static uint32_t lfo_last_pos;
static void lfo_tick(uint32_t us, int running)
{
    int s, i, t, l;
    uint32_t adv = gpos - lfo_last_pos;
    lfo_last_pos = gpos;
    for (s = 0; s < S4_NSEQ; s++) {
        rt_t *r = &rt[s];
        for (t = 0; t < S4_NTRK; t++)
            for (i = 0; i < S4_D_N; i++)
                dm[s][t][i] = dmap[s][i];
        for (i = 0; i < S4_NLFO; i++) {
            s4_lfo_t *f = &s4[s].lfo[i];
            float v, ph;
            int amt = f->amt, rate = f->rate;
            if (!f->amt && (!f->xtype || !f->xamt))
                continue;
            /* the LFOs move each other's wave, rate and amount */
            rate = clampi(rate + dm[s][0][i ? S4_D_LFO2_RATE : S4_D_LFO1_RATE], 0, 59);
            amt = clampi(amt + dm[s][0][i ? S4_D_LFO2_AMT : S4_D_LFO1_AMT], 0, 100);
            if (running || rate >= LFO_SYNC) {
                float d = rate < LFO_SYNC ? (float)adv / (float)LFO_SYNC_U[rate] : (float)us * 1e-6f * lfo_hz(rate);
                ph = r->lph[i] + d;
                if (ph >= 1.0f) {
                    ph -= fm_floorf(ph);
                    r->lsh2[i] = r->lsh[i];
                    r->lsh[i] = (float)(int32_t)(s4_rand() >> 1) * (1.0f / 1073741824.0f) - 1.0f;
                }
                r->lph[i] = ph;
            }
            ph = r->lph[i] + (float)f->phase * (1.0f / 128.0f);
            ph -= fm_floorf(ph);
            v = lfo_wave(clampi(f->wave + dm[s][0][i ? S4_D_LFO2_WAVE : S4_D_LFO1_WAVE], 0, W_N - 1), ph, r->lsh[i], r->lsh2[i]);
            r->lval[i] = v;
            if (f->idest) {
                int c = (int)(v * (float)amt + (v < 0 ? -0.5f : 0.5f)) + f->ofs;
                for (t = 0; t < S4_NTRK; t++)
                    if (lfo_track_on(s, t, i))
                        dm_add(s, t, f->idest, c);
            }
            if (f->xtype && running && now_us - r->lsent[i] >= 2000u) {   /* MIDI: at most every 2 ms */
                int xa = f->xamt, v7 = clampi((int)((v + 1.0f) * 0.5f * 127.0f * (float)xa / 100.0f) + f->ofs, 0, 127);
                int v14 = clampi(8192 + (int)(v * 8191.0f * (float)xa / 100.0f), 0, 16383);
                uint8_t before = r->lcc[i];
                send_x(s, 0, f->xtype, f->xnum, f->xtype == X_PB ? v14 >> 7 : v7, v14, &r->lcc[i]);
                if (r->lcc[i] != before || f->xtype == X_PB)
                    r->lsent[i] = now_us;
            }
        }
        /* the mod lanes */
        for (l = 0; l < lanes_in_use(s); l++) {
            s4_modl_t *m = &s4[s].ml[l];
            int v, st = lane_step(s, l), tk = lane_track(s, l), k;
            float target;
            if (!m->on)
                continue;
            if (!s4[s].ml[l].linked && running)
                lane_clock(s, l);
            v = clampi((int)s4[s].mod[l][st & 127] + m->offset, m->mn, m->mx ? m->mx : 127);
            target = (float)v;
            if (m->smooth && running) {        /* slewed: a time constant from the setting */
                float k1 = 1.0f / (1.0f + (float)m->smooth * (float)m->smooth * 0.05f);
                r->msm[l] += (target - r->msm[l]) * k1;
            } else {
                r->msm[l] = target;
            }
            v = (int)(r->msm[l] + 0.5f);
            for (k = 0; k < 2; k++)
                if (m->idest[k])
                    dm_add(s, tk, m->idest[k], ((v + m->iofs[k]) * m->iamt[k]) / 127);
            if (m->xtype && running) {
                int muted = tk >= 0 && s4[s].mode == M_MULTI && s4[s].d.multi.t[tk].mute && !s4g.mute_cc;
                if (!muted && !s4[s].mute)
                    send_x(s, tk < 0 ? 0 : tk, m->xtype, m->xnum, v, v << 7, &r->mlast[l]);
            }
        }
    }
}
static void lfo_retrig(int s)
{
    int i;
    for (i = 0; i < S4_NLFO; i++)
        if (s4[s].lfo[i].retrig)
            rt[s].lph[i] = 0.0f;
}

/* -------------------------------------------------------------------------------- mappings --- */
static void map_param(int s, int p, int v)    /* a setting changed for good by a mapping */
{
    s4_seq_t *q = &s4[s];
    switch (p) {
    case S4_P_ENABLE: mute_set(s, v < 64); break;
    case S4_P_MUTE: q->mute = (uint8_t)(v >= 64); break;
    case S4_P_DIV: q->div = (uint8_t)clampi(DV_1BAR + v, DV_1BAR, DV_48); break;
    case S4_P_DIR: q->dir = (uint8_t)clampi(v, 0, 3); break;
    case S4_P_SCALE: q->scale = (uint8_t)clampi(v, 0, S4_NSCALE - 1); break;
    case S4_P_ROOT: q->root = (uint8_t)clampi(v, 0, 11); break;
    case S4_P_SWING: q->swing = (uint8_t)clampi(50 + v, 50, 75); break;
    case S4_P_LOOP:
        q->loop_on = 1;
        q->loop_a = (uint8_t)rt[s].c[0].pos;
        q->loop_b = (uint8_t)clampi(rt[s].c[0].pos + clampi(v, 1, 128) - 1, 0, 127);
        break;
    case S4_P_LOOP_OFF: q->loop_on = 0; break;
    case S4_P_BEND: q->bend = (int8_t)clampi(v, -50, 50); break;
    case S4_P_NUDGE: q->nudge = (int8_t)clampi(v, -100, 100); break;
    case S4_P_INIT: q->init = (uint8_t)clampi(v, 0, q->end); break;
    case S4_P_END: q->end = (uint8_t)clampi(v, q->init, 127); break;
    case S4_P_ML_ON: q->ml[0].on = (uint8_t)(v >= 1); break;
    case S4_P_ML_OFS: q->ml[0].offset = (int8_t)clampi(v, -100, 100); break;
    case S4_P_RP_VEL: q->rp_vel = (int8_t)clampi(v, -100, 100); break;
    case S4_P_RP_OCT: q->rp_oct = (int8_t)clampi(v, -100, 100); break;
    case S4_P_RP_TRIG: q->rp_trig = (uint8_t)clampi(v, 0, 100); break;
    case S4_P_RP_RTG: q->rp_rtg = (int8_t)clampi(v, -100, 100); break;
    case S4_P_TEMPO: s4g.tempo = (uint16_t)clampi(v * 10, 300, 3000); break;
    case S4_P_FILL: fill_on = (uint8_t)(v >= 1); s4p.fill = fill_on; break;
    case S4_P_RESET: if (v >= 1) seq_reset(s, rt[s].lpos); break;
    case S4_P_GENERATE: if (v >= 1) s4p.last_in_src = 0xF0; break;   /* (the UI runs the generator: flagged) */
    case S4_P_RELOAD: if (v >= 1) s4p.last_in_src = 0xF1; break;
    case S4_P_TAP: if (v >= 1) tap(); break;
    case S4_P_REC: rec_set(v >= 1); break;
    default: break;
    }
}

volatile uint8_t s4_pc_req;                    /* a program change asks for pattern n - 1 (the UI loads it) */
static void midi_cc_in(int cable, int st, int d1, int d2)
{
    int s, i, ch = st & 15, kind = st & 0xF0, consumed = 0;
    (void)cable;
    for (s = 0; s < S4_NSEQ; s++)
        for (i = 0; i < S4_NMAP; i++) {
            s4_map_t *m = &s4[s].map[i];
            int v, k;
            if (m->type != 1 || kind != 0xB0 || m->num != d1)
                continue;
            if (!(m->ch == 17 || (m->ch == 0 && ch == s4[s].ch) || (m->ch >= 1 && m->ch <= 16 && ch == m->ch - 1)))
                continue;
            v = d2;
            map_apply(s, m, v);
            (void)k;
            if (m->consume)
                consumed = 1;
        }
    if (consumed)
        return;
    if (kind == 0xB0 && s4g.tcc_ch && ch == s4g.tcc_ch - 1 && d1 >= 105 && d1 <= 107) {
        transport_cc(d1, d2);
        return;
    }
    if (kind == 0xC0 && s4g.pc_ch && ch == s4g.pc_ch - 1 && d1 < S4_NPAT) {
        s4_pc_req = (uint8_t)(d1 + 1);
        return;
    }
    rec_cc(ch, kind, d1, d2);                  /* recording: into a mod lane that sends this */
}
static void map_apply(int s, const s4_map_t *m, int v)
{
    int k;
    if (m->fwd) {
        int port, ch, o = clampi(v * (m->iamt[0] ? m->iamt[0] : 127) / 127 + m->iofs[0], 0, 127);
        trk_port(s, 0, &port, &ch);
        out3(port, 0xB0 | ch, m->fwd - 1, o);
    }
    for (k = 0; k < 2; k++)
        if (m->idest[k])
            dmap[s][m->idest[k]] = (int16_t)(v * m->iamt[k] / 127 + m->iofs[k]);
    for (k = 0; k < 2; k++)
        if (m->para[k])
            map_param(s, m->para[k], v * m->pamt[k] / 127 + m->pofs[k]);
}
static int map_note(int s, int ch, int note, int vel)   /* a note a mapping takes (1: consumed) */
{
    int i, used = 0;
    for (i = 0; i < S4_NMAP; i++) {
        s4_map_t *m = &s4[s].map[i];
        if ((m->type != 2 && m->type != 3) || m->num != note)
            continue;
        if (!(m->ch == 17 || (m->ch == 0 && ch == s4[s].ch) || (m->ch >= 1 && m->ch <= 16 && ch == m->ch - 1)))
            continue;
        if ((m->type == 2 && vel) || (m->type == 3 && !vel) || (m->type == 2 && !vel))
            map_apply(s, m, m->type == 2 ? (vel ? 127 : 0) : 0);
        if (m->consume)
            used = 1;
    }
    return used;
}
