/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4's project: what is kept in flash, and when. Part of the unity build (after the platform) and of the
 * host simulator.
 *   the project      the settings (tempo, clock, MIDI, the ways of working), the pattern each sequencer has
 *                    loaded, where the screen was
 *   the sequencers   as they are, changes and all (their working copies): back as they were after a restart
 *   the patterns     16 a sequencer, saved when asked (SAVE); loading one stages it, and the engine swaps it
 *                    in at its boundary
 *   the songs        the arranger's ten
 * Changes are kept a few seconds after the last, while stopped (the flash pauses everything when it
 * erases; programming, as a save while playing does, only for a moment). */

proj_t proj;
static uint8_t dirty_seq, dirty_proj, dirty_songs;
static uint32_t last_change;
#define pk s4_packbuf                          /* a sequencer packed (the engine's buffer, main loop only) */
static uint8_t staged[S4_NSEQ];                /* a pattern staged and queued: its number + 1 */

static int clampi_p(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void project_dirty_seq(int s)
{
    dirty_seq |= (uint8_t)(1u << (s & 3));
    last_change = plat_ms();
}
void project_dirty_proj(void)
{
    dirty_proj = 1;
    last_change = plat_ms();
}
void project_dirty_songs(void)
{
    dirty_songs = 1;
    last_change = plat_ms();
}
int project_is_dirty(void) { return dirty_seq || dirty_proj || dirty_songs; }

/* ---------------------------------------------------------------------------------- the demo --- */
static void mono(int s, int i, int deg, int vel, int gate)
{
    s4_step_t *st = &s4[s].d.mono[i];
    st->deg = (int8_t)deg;
    st->vel = (uint8_t)vel;
    st->gate = (uint8_t)gate;
    st->oct = 0;
    st->cond = 100;
    st->rtg = 0;
    st->ofs = 0;
    st->glide = 0;
}
static void drum(int s, int t, int i, int vel)
{
    s4_step_t *st = &s4[s].d.multi.s[t][i];
    st->deg = 0;
    st->vel = (uint8_t)vel;
    st->gate = 40;
    st->cond = 100;
    st->rtg = 0;
    st->ofs = 0;
    st->oct = 0;
}
static void chord(int s, int i, int deg, int type, int voi)
{
    s4_cstep_t *c = &s4[s].d.chord[i];
    c->deg = (int8_t)deg;
    c->type = (uint8_t)type;
    c->voi = (uint8_t)voi;
    c->spread = 1;
    c->vel = 92;
    c->gate = 96;
    c->cond = 100;
    c->oct = 0;
}
void project_demo(void)
{
    int s, i;
    s4_defaults_glob();
    s4g.tempo = 1180;
    /* 1: a bass in A minor, Mono, channel 1 */
    s4_defaults_seq(0, M_MONO);
    s4[0].scale = 2;
    s4[0].root = 9;
    s4[0].oct = 1;
    {
        static const int8_t B[16] = {0, -1, 0, 7, -1, 0, 4, -1, 0, -1, 0, 7, 6, -1, 4, 2};
        for (i = 0; i < 16; i++)
            if (B[i] >= 0)
                mono(0, i, B[i] == 7 ? 0 : B[i], i % 4 == 0 ? 110 : 88, i % 4 == 0 ? 70 : 45);
        s4[0].d.mono[3].oct = 1;
        s4[0].d.mono[11].oct = 1;
        s4[0].d.mono[12].glide = 40;
    }
    s4[0].glide_cc = 0;
    s4[0].rp_vel = 12;
    /* 2: chords, a bar each: i VI III VII, voiced near one another, channel 2 */
    s4_defaults_seq(1, M_CHORD);
    s4[1].scale = 2;
    s4[1].root = 9;
    s4[1].oct = 2;
    s4[1].div = DV_1BAR;
    s4[1].end = 3;
    chord(1, 0, 0, 1, 0);
    chord(1, 1, 5, 0, 0);
    chord(1, 2, 2, 0, 0);
    chord(1, 3, 6, 2, 0);
    s4[1].ch_auto = 1;
    s4[1].lfo[0].xtype = X_CC;
    s4[1].lfo[0].xnum = 74;
    s4[1].lfo[0].xamt = 40;
    s4[1].lfo[0].rate = 3;                     /* two bars */
    s4[1].lfo[0].ofs = 40;
    /* 3: drums, Multitrack, General MIDI on channel 10 */
    s4_defaults_seq(2, M_MULTI);
    for (i = 0; i < 16; i++) {
        if (i % 4 == 0)
            drum(2, 0, i, i == 0 ? 120 : 108);
        if (i == 4 || i == 12)
            drum(2, 1, i, 112);
        if (i % 2 == 0)
            drum(2, 2, i, i % 4 == 2 ? 100 : 72);
    }
    drum(2, 0, 10, 92);
    drum(2, 3, 14, 90);
    drum(2, 4, 15, 70);
    s4[2].d.multi.s[2][14].vel = 0;
    s4[2].d.multi.s[1][15].vel = 60;
    s4[2].d.multi.s[1][15].cond = S4_C_AB + 1;  /* 2:2 */
    s4[2].d.multi.s[1][15].gate = 30;
    s4[2].d.multi.s[1][15].deg = 0;
    s4[2].d.multi.t[2].rp_vel = 20;
    s4[2].swing = 56;
    /* 4: a lead that follows the chords (the harmonizer on sequencer 2), channel 4 */
    s4_defaults_seq(3, M_MONO);
    s4[3].scale = S4_HARMO;
    s4[3].leader = 1;
    s4[3].oct = 3;
    {
        static const int8_t L[16] = {2, -1, 3, -1, 4, -1, 3, 2, -1, -1, 4, -1, 5, 4, 3, -1};
        for (i = 0; i < 16; i++)
            if (L[i] >= 0)
                mono(3, i, L[i], 96, i == 7 ? S4_TIE : 60);
        mono(3, 8, 2, 96, 80);
    }
    s4[3].d.mono[14].cond = 60;
    s4[3].karp.mode = A_UP;
    s4[3].mute = 0;
    for (s = 0; s < S4_NSEQ; s++)
        s4[s].ch = (uint8_t)s;
    s4[2].ch = 9;
    for (s = 0; s < S4_NSEQ; s++) {
        proj.pat[s] = 0;
        proj.trk[s] = 0;
        proj.koct[s] = 0;
        proj.row[s] = 0;
        proj.page[s] = 0;
    }
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.sel = 0;
    proj.song = 0;
    proj.kvel = 100;
    for (i = 0; i < S4_NSONG; i++) {           /* a first song: each lane's pattern 1, then 2 */
        int k;
        for (s = 0; s < S4_NSEQ; s++)
            for (k = 0; k < S4_NSLOT; k++) {
                s4song[i].slot[s][k].pat = S4_EMPTY;
                s4song[i].slot[s][k].rep = 1;
                s4song[i].slot[s][k].pc = 0;
                s4song[i].slot[s][k].bank = 0;
            }
        s4song[i].master = 0;
        s4song[i].launchq = 0;
    }
    for (s = 0; s < S4_NSEQ; s++) {
        s4song[0].slot[s][0].pat = 0;
        s4song[0].slot[s][0].rep = 4;
    }
    dirty_seq = 0xF;
    dirty_proj = 1;
    dirty_songs = 1;
}

/* --------------------------------------------------------------------------- loading, saving --- */
int project_load(void)
{
    int s, n;
    if (plat_log_load(KEY_PROJ, &proj, sizeof proj) != (int)sizeof proj || proj.magic != PROJ_MAGIC ||
        proj.format != PROJ_FORMAT) {
        project_demo();
        for (s = 0; s < S4_NSEQ; s++)          /* the demo in its slots too: pattern 1 of each */
            project_save_pattern(s, 0);
        return -1;
    }
    s4g = proj.g;
    if (plat_log_load(KEY_SONGS, s4song, sizeof s4song) != (int)sizeof s4song)
        for (n = 0; n < S4_NSONG; n++) {
            int k;
            for (s = 0; s < S4_NSEQ; s++)
                for (k = 0; k < S4_NSLOT; k++)
                    s4song[n].slot[s][k].pat = S4_EMPTY;
        }
    for (s = 0; s < S4_NSEQ; s++) {
        n = plat_log_load(KEY_WORK(s), pk, sizeof pk);
        if (n > 0 && !s4_unpack(&s4[s], pk, (uint32_t)n))
            continue;
        n = plat_log_load(KEY_PAT(s, proj.pat[s] % S4_NPAT), pk, sizeof pk);
        if (n > 0 && !s4_unpack(&s4[s], pk, (uint32_t)n))
            continue;
        s4_defaults_seq(s, M_MONO);
    }
    for (s = 0; s < S4_NSEQ; s++)
        s4p.pat[s] = proj.pat[s];
    dirty_seq = 0;
    dirty_proj = 0;
    dirty_songs = 0;
    return 0;
}

static int save_work(int s)
{
    uint32_t n = s4_pack(&s4[s], pk, sizeof pk);
    if (!n)
        return -3;
    return plat_log_save(KEY_WORK(s), pk, n);
}
static int save_proj(void)
{
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.g = s4g;
    return plat_log_save(KEY_PROJ, &proj, sizeof proj);
}
int project_save_all(void)
{
    int s, rc = 0;
    for (s = 0; s < S4_NSEQ; s++)
        if (dirty_seq >> s & 1u) {
            int r = save_work(s);
            if (r)
                rc = r;
            else
                dirty_seq &= (uint8_t)~(1u << s);
        }
    if (dirty_songs) {
        int r = plat_log_save(KEY_SONGS, s4song, sizeof s4song);
        if (r)
            rc = r;
        else
            dirty_songs = 0;
    }
    if (dirty_proj || !rc) {
        int r = save_proj();
        if (r)
            rc = r;
        else
            dirty_proj = 0;
    }
    return rc;
}
int project_save_pattern(int s, int pat)
{
    uint32_t n = s4_pack(&s4[s], pk, sizeof pk);
    int rc;
    if (!n)
        return -3;
    rc = plat_log_save(KEY_PAT(s, pat), pk, n);
    if (rc)
        return rc;
    rc = plat_log_save(KEY_WORK(s), pk, n);
    if (!rc)
        dirty_seq &= (uint8_t)~(1u << s);
    proj.pat[s] = (uint8_t)pat;
    s4p.pat[s] = (uint8_t)pat;
    dirty_proj = 1;
    return rc;
}
int project_has_pattern(int s, int pat) { return plat_log_has(KEY_PAT(s, pat)); }
int project_clear_pattern(int s, int pat) { return plat_log_save(KEY_PAT(s, pat), "", 0); }
int project_copy_pattern(int s, int from, int to_s, int to)
{
    int n = plat_log_load(KEY_PAT(s, from), pk, sizeof pk);
    if (n <= 6)
        return -1;
    if (to_s != s) {                           /* to another sequencer: it keeps its own channel and port */
        pk[6 + offsetof(s4_seq_t, ch)] = s4[to_s].ch;   /* (the packed settings follow a 6-byte header) */
        pk[6 + offsetof(s4_seq_t, port)] = s4[to_s].port;
    }
    return plat_log_save(KEY_PAT(to_s, to), pk, (uint32_t)n);
}

/* a slot into the staging buffer, the engine told: in at the boundary (now, when stopped) */
int project_load_pattern(int s, int pat)
{
    int n, empty = 0;
    n = plat_log_load(KEY_PAT(s, pat), pk, sizeof pk);
    s4_lock(1);                                /* (the engine may be swapping the last one in right now) */
    if (n <= 0 || s4_unpack(&s4_stage[s], pk, (uint32_t)n)) {   /* empty: the same settings, no steps */
        s4_stage[s] = s4[s];
        {
            int i;
            switch (s4_stage[s].mode) {
            case M_MONO:
                for (i = 0; i < S4_NSTEP; i++)
                    s4_stage[s].d.mono[i].vel = 0;
                break;
            case M_MULTI: {
                int t;
                for (t = 0; t < S4_NTRK; t++)
                    for (i = 0; i < S4_NSTEP; i++)
                        s4_stage[s].d.multi.s[t][i].vel = 0;
                break;
            }
            case M_POLY: s4_stage[s].d.poly.n = 0; break;
            case M_CHORD:
                for (i = 0; i < S4_NSTEP; i++)
                    s4_stage[s].d.chord[i].vel = 0;
                break;
            default: break;
            }
            for (i = 0; i < S4_NMOD; i++) {
                int k;
                for (k = 0; k < S4_NSTEP; k++)
                    s4_stage[s].mod[i][k] = 0;
            }
        }
        empty = 1;
    }
    s4_lock(0);
    staged[s] = (uint8_t)(pat + 1);
    proj.pat[s] = (uint8_t)pat;
    s4_queue_pattern(s, pat);
    dirty_proj = 1;
    dirty_seq |= (uint8_t)(1u << s);
    last_change = plat_ms();
    return empty;
}

int project_autosave(int stopped)
{
    if (!project_is_dirty() || !stopped || plat_ms() - last_change < 3000u)
        return 0;
    project_save_all();
    return 1;
}

/* in the main loop: what the engine asked for, and the flash's tidying when nothing is going on */
void project_service(void)
{
    int s;
    static uint32_t gc_t;
    if (s4_pc_req) {                           /* a program change: that pattern on all four */
        int p = s4_pc_req - 1;
        s4_pc_req = 0;
        for (s = 0; s < S4_NSEQ; s++)
            project_load_pattern(s, p);
    }
    for (s = 0; s < S4_NSEQ; s++) {
        if (s4p.want[s] != 0xFF) {             /* the arranger's next */
            int p = s4p.want[s];
            s4p.want[s] = 0xFF;
            project_load_pattern(s, p);
        }
        if (staged[s] && s4p.next[s] == 0xFF && s4p.pat[s] == staged[s] - 1)
            staged[s] = 0;
    }
    if (!s4p.on && plat_ms() - last_change > 5000u && plat_ms() - gc_t > 200u && plat_log_gc_due()) {
        gc_t = plat_ms();
        plat_log_gc();                         /* a sector at a time, a fifth of a second apart */
    }
    (void)clampi_p;
}
