/* SPDX-License-Identifier: GPL-3.0-only */
/* GX-1: the GEN pages (part of ui.c, after ui_draw.c): GEN, TRACK and ARRANGE, as the phone player. HOME steps
 * through them; the tabs at the top say which is on. Sections are blocked out by tone (chrome, grey where you
 * edit, near-black for the controls), the selected thing in the app's colour.
 *
 *   y   0 ..  15  the song line: the style, the key, the part; the tempo and the loop's 8 bars
 *   y  16 ..  31  the pages: GEN / TRACK / ARRANGE (the open one the colour of what it opens)
 *   GEN      32..83 the four tracks (this bar) · 84..99 GEN: the idea · 100..171 the macros (K1-K4) ·
 *            172..239 the selected track's 16 steps (the white keys)
 *   TRACK    32..47 the track tabs (black keys 1-4) · 48..99 the editor: the drum lanes, or the steps ·
 *            100..171 this track's own macros (K1-K4; grey: still following GEN) · 172..239 MUTATE
 *   ARRANGE  32..99 the energy of the six parts · 100..171 the parts (W1-W6) · 172..239 the loop
 *
 * Keys and controls:
 *   HOME              the next page (twice: the patterns)
 *   white keys        the selected track's steps (tap on / off, hold for its values); ARRANGE: W1-W6 queue a part
 *   GLO + W1-W4, W5   mutate the selected track: rhythm, pitch, velocity, length; all
 *   GLO + W9-W14      the six parts (each genre names its own)
 *   black 1-4 · 5-8   select drums, sub, chords, upper · mute them
 *   black 9, 10       the page (bar) back and on
 *   K1-K4             GEN: density, syncopation, space, groove (GLO: humanise, tension, complexity, evolution);
 *                     TRACK: the same four for the selected track only
 *   ALGORITHM         the genre (a new idea in it when the knob rests)
 *   PRESETS           the idea number (a new idea from it when the knob rests; locked tracks stay)
 *   SELECT            the tempo (the genre's range is marked, not enforced: ADR-028)
 *   OCT- / OCT+       the key, a semitone (everything pitched moves; the drums stay)
 *   FX new idea (GLO: re-roll the selected track) · SEL mutate · ENV lock · EDIT undo (GLO: redo)
 *   SEQ the grid (HOME comes back) */

#define GN_BG RGB(11, 11, 13)
#define GN_PANEL RGB(30, 30, 35)
#define GN_LINE RGB(46, 46, 52)
#define GN_OFF RGB(58, 58, 66)
#define GN_MUTED RGB(139, 139, 147)
#define GN_INK RGB(236, 236, 240)
#define GN_ACC RGB(167, 139, 250)              /* the app's colour (the phone's): the selected thing */
#define GN_CHROME RGB(30, 30, 34)             /* the tabs, the song line */
#define GN_EDIT RGB(23, 23, 27)               /* where you edit: the tracks, the editor, the parts */

static struct {
    uint8_t genre;                     /* the genre picked (gx_genre_t) */
    uint8_t fam_style[8];              /* the style last used in each family (+1; 0: its first) */
    uint32_t seed;
    uint8_t gpend, spend;              /* ALGORITHM / PRESETS turned: a new idea when it rests */
    uint32_t gpend_t, spend_t;
    uint8_t mac[GX_NMACRO];            /* the macros as turned (applied when the knob rests) */
    uint8_t mpend;                     /* bit per macro: turned, not applied yet */
    uint32_t mpend_t;
    uint16_t wshift;                   /* white keys pressed with GLO (their release is theirs) */
    uint32_t sig[3];
    uint8_t drawn;                     /* the page is on screen (else everything is drawn again) */
    uint8_t pg;                        /* 0 GEN, 1 TRACK, 2 ARRANGE */
    uint8_t tmac[4], tmpend, trole;    /* TRACK's K1-K4 as turned, not applied yet; the track they are for */
    uint32_t tmpend_t;
} gen;

static uint16_t gn_c(int r) { (void)r; return GN_ACC; }
static uint16_t gn_dim(int r) { (void)r; return RGB(104, 104, 114); }   /* an unselected track: grey (colour is for the selected one) */

static const char *gn_genre_label(int g)
{
    static char t[16];
    const gx_genre_prof_t *p = gx_profile((gx_genre_t)g);
    const char *n = p ? p->name : "?";
    int k = 0;
    for (; n[k] && k < 15; k++)
        t[k] = (char)(n[k] >= 'a' && n[k] <= 'z' ? n[k] - 32 : n[k]);
    t[k] = 0;
    return t;
}

/* families (profiles/families.toml): ALGORITHM the family, GLO + ALGORITHM the style in it */
static int gn_family_of(int g)
{
    uint32_t f, k;
    for (f = 0; f < gx_nfamily; f++)
        for (k = 0; k < gx_families[f].n; k++)
            if (gx_families[f].g[k] == g)
                return (int)f;
    return 0;
}
static int gn_style_index(int f, int g)
{
    int k;
    for (k = 0; k < gx_families[f].n; k++)
        if (gx_families[f].g[k] == g)
            return k;
    return 0;
}
static const char *gn_track_label(int r)
{
    static const char *const N[3] = {"Drums", "Sub", "Chords"};
    const gx_genre_prof_t *p = gx_profile((gx_genre_t)(gxb_ok ? gxb_comp.r[GX_ROLE_UPPER].genre : gen.genre));
    if (r < 3)
        return N[r];
    return !p ? "Upper" : p->upper.mode == GX_UPPER_MID ? "Mid" : p->upper.mode == GX_UPPER_MOTIF ? "Motif" : "Arp";
}
static int gn_macro(int m)                       /* GEN's: the value of the tracks that follow it */
{
    int r;
    if (gen.mpend >> m & 1u)
        return gen.mac[m];
    if (!gxb_ok)
        return 64;
    for (r = 0; r < 4; r++)
        if (!(gxb_own[r] >> m & 1u))
            return gxb_comp.r[r].macro[m];
    return gxb_comp.r[0].macro[m];
}
static int gn_owned(int r, int m) { return gxb_ok && (gxb_own[r] >> m & 1u); }
static int gn_tmacro(int m)                      /* the selected track's own */
{
    if ((gen.tmpend >> m & 1u) && gen.trole == sel())
        return gen.tmac[m];
    return gxb_ok ? gxb_comp.r[sel()].macro[m] : 64;
}
static void gn_seed_text(char *t);
static int gn_locked(int r) { return gxb_ok && (gxb_comp.lock >> r & 1u); }

/* ---- the key, the chords, the tempo range (ADR-028) ---- */
static const char *const GN_NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const uint8_t GN_SCALE[4][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}};
static const gx_genre_prof_t *gn_prof(void) { return gx_profile((gx_genre_t)(gxb_ok ? gxb_comp.r[GX_ROLE_CHORDS].genre : gen.genre)); }
static int gn_scale(void) { const gx_genre_prof_t *p = gn_prof(); return p && p->harm.scale < 4 ? p->harm.scale : 0; }
static int gn_key(void) { const gx_genre_prof_t *p = gn_prof(); return gxb_ok ? gxb_comp.key : p ? p->harm.key : 0; }
static void gn_key_text(char *t, int full)        /* "F#m" / "F# minor" */
{
    static const char *const S[4] = {"m", "dor", "phr", ""}, *const F[4] = {" minor", " dorian", " phrygian", " major"};
    scopy(t, GN_NOTE[gn_key() % 12], 16);
    scat(t, full ? F[gn_scale()] : S[gn_scale()], 16);
}
static int gn_semi(int d) { return GN_SCALE[gn_scale()][d % 7] + 12 * (d / 7); }
static void gn_numeral(int bar, char *t)          /* the scale's own triad on the bar's degree: i, VI, ii° */
{
    static const char *const U[7] = {"I", "II", "III", "IV", "V", "VI", "VII"}, *const L[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    int d = gxb_harm.deg[bar & 31] % 7, third = gn_semi(d + 2) - gn_semi(d), fifth = gn_semi(d + 4) - gn_semi(d);
    scopy(t, third == 3 ? L[d] : U[d], 8);
    if (third == 3 && fifth == 6)
        scat(t, "o", 8);
}
static void gn_chord_text(int bar, char *t)       /* "Dmaj7", "F#m9" */
{
    int root = gxb_harm.root[bar & 31] % 12, pcs = gxb_harm.pcs[bar & 31];
#define GN_HAS(i) ((pcs >> ((root + (i)) % 12)) & 1)
    const char *q = GN_HAS(3) ? "m" : GN_HAS(4) ? "" : GN_HAS(2) ? "sus2" : "5", *ext = "";
    if (GN_HAS(3) && GN_HAS(6) && !GN_HAS(7))
        q = "dim";
    if (GN_HAS(10))
        ext = GN_HAS(5) && q[0] == 'm' && !q[1] ? "11" : (GN_HAS(2) && q[0] != 's' ? "9" : "7");
    else if (GN_HAS(11))
        ext = "maj7";
    else if (GN_HAS(2) && q[0] != 's')
        ext = "add9";
#undef GN_HAS
    scopy(t, GN_NOTE[root], 16);
    scat(t, q, 16);
    scat(t, ext, 16);
}
static int gn_tempo_out(void)                     /* the tempo is outside the genre's range */
{
    const gx_genre_prof_t *p = gn_prof();
    return p && (s4g.tempo < p->tempo_min_x10 || s4g.tempo > p->tempo_max_x10);
}

/* a track's bar: each step's velocity where a note starts (the loudest), and its length in steps */
static void gn_bar(int s, int bar, uint8_t *vel, uint8_t *len)
{
    const s4_seq_t *q = &s4[s];
    int i, k, b0 = bar * 16;
    for (i = 0; i < 16; i++)
        vel[i] = len[i] = 0;
    if (q->mode == M_POLY) {
        for (k = 0; k < q->d.poly.n; k++) {
            const s4_pnote_t *p = &q->d.poly.note[k];
            int st = p->step, j, n = 1, cont = 0;
            if (st < b0 || st >= b0 + 16)
                continue;
            for (j = 0; j < q->d.poly.n && st > 0; j++)      /* the same note tied in: not a start */
                if (q->d.poly.note[j].step == st - 1 && q->d.poly.note[j].deg == p->deg &&
                    q->d.poly.note[j].gate == S4_TIE)
                    cont = 1;
            if (cont)
                continue;
            if (p->gate == S4_TIE) {                           /* how far it is tied */
                int at = st, more = 1;
                while (more && n < 16) {
                    more = 0;
                    for (j = 0; j < q->d.poly.n; j++)
                        if (q->d.poly.note[j].step == at + 1 && q->d.poly.note[j].deg == p->deg) {
                            n++;
                            at++;
                            more = q->d.poly.note[j].gate == S4_TIE;
                            break;
                        }
                }
            }
            if (p->vel > vel[st - b0])
                vel[st - b0] = p->vel;
            if (n > len[st - b0])
                len[st - b0] = (uint8_t)n;
        }
    } else if (q->mode == M_MONO) {
        for (i = 0; i < 16; i++) {
            int st = b0 + i, n = 1;
            const s4_step_t *x = &q->d.mono[st];
            if (!x->vel || (st > 0 && q->d.mono[st - 1].vel && q->d.mono[st - 1].gate == S4_TIE))
                continue;
            while (st + n - 1 < (int)S4_NSTEP - 1 && q->d.mono[st + n - 1].gate == S4_TIE && n < 16)
                n++;
            vel[i] = x->vel;
            len[i] = (uint8_t)n;
        }
    } else {
        for (i = 0; i < 16; i++)
            if (step_on(s, 0, b0 + i)) {
                vel[i] = 100;
                len[i] = 1;
            }
    }
}

static int gn_playing_step(int s) { return s4p.on ? s4p.step[s][0] : -1; }

/* ---------------------------------------------------------------------------------- drawing --- */
static void gn_lock_icon(int32_t x, int32_t y, uint16_t c)     /* 7 x 9 */
{
    frame(x + 1, y, 5, 5, c);
    cv_rect(x, y + 4, 7, 5, c);
}

/* the macros as pictures (52 x 30), drawn like a synth's panel: grey guides for where things start (the beat, the
   baseline, the loop as written), the track's colour for what the knob does, 2 px lines and a square handle on
   the value. Each macro its own picture. web/player.html draws the same geometry (macroPic). */
#define GN_GUIDE RGB(104, 104, 114)
static const int8_t GN_SIN[64] = {0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126,127,126,125,122,117,112,106,98,90,81,71,60,49,37,25,12,0,-12,-25,-37,-49,-60,-71,-81,-90,-98,-106,-112,-117,-122,-125,-126,-127,-126,-125,-122,-117,-112,-106,-98,-90,-81,-71,-60,-49,-37,-25,-12};
static int32_t gn_isin(int32_t ph) { return GN_SIN[(ph >> 2) & 63]; }        /* 256 a cycle, +-127 */
static void gn_ln(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* a 2 px line */
{
    cv_line(x0, y0, x1, y1, c);
    if (x0 == x1)
        cv_line(x0 + 1, y0, x1 + 1, y1, c);
    else
        cv_line(x0, y0 + 1, x1, y1 + 1, c);
}
static void gn_handle(int32_t x, int32_t y, uint16_t c) { cv_rect(x - 2, y - 2, 5, 5, c); }
static void gn_vdash(int32_t x, int32_t y0, int32_t y1, uint16_t c)
{
    for (; y0 < y1; y0 += 4)
        cv_rect(x, y0, 1, y0 + 2 <= y1 ? 2 : 1, c);
}
static void gn_macro_pic(int m, int v, int32_t x, int32_t y, uint16_t acc)
{
    int i;
    const int32_t b = y + 28;                    /* the baseline */
    switch (m) {
    case 0: {                                    /* density: how many of eight voices play */
        int n = 1 + v * 8 / 128;
        cv_rect(x, b + 1, 52, 1, GN_GUIDE);
        for (i = 0; i < 8; i++) {
            int32_t lx = x + 2 + i * 7;
            if (i < n)
                cv_rect(lx, y + 4, 2, b - y - 4, acc);
            else
                cv_rect(lx, b - 4, 2, 4, GN_GUIDE);
        }
        gn_handle(x + 3 + (n - 1) * 7, y + 3, acc);
        break;
    }
    case 1: {                                    /* sync: the pulses slide off the beat (grey) */
        int32_t sh = v * 6 / 127, px = x, p = x;
        for (i = 0; i < 4; i++)
            gn_vdash(x + 2 + i * 12, y + 2, b, GN_GUIDE);
        for (i = 0; i < 4; i++) {
            p = x + 2 + i * 12 + sh;
            gn_ln(px, b, p, b, acc);
            gn_ln(p, b, p, y + 6, acc);
            gn_ln(p, y + 6, p + 5, y + 6, acc);
            gn_ln(p + 5, y + 6, p + 5, b, acc);
            px = p + 5;
        }
        gn_ln(px, b, x + 51, b, acc);
        gn_handle(p, y + 6, acc);
        break;
    }
    case 2: {                                    /* space: the silence after a hit (a decay that grows) */
        int32_t len = 8 + v * 36 / 127, px = x + 2, py = y + 2, k;
        gn_ln(x + 2, b, x + 2, y + 2, acc);
        for (k = 1; k <= len; k++) {
            int32_t u = len - k;                 /* (1 - t)^3, t = k / len */
            int32_t yy = b - (b - y - 2) * u * u * u / (len * len * len);
            gn_ln(px, py, x + 2 + k, yy, acc);
            px = x + 2 + k;
            py = yy;
        }
        cv_rect(px, b, x + 52 - px, 1, GN_GUIDE);
        gn_handle(px, b, acc);
        break;
    }
    case 3: {                                    /* groove: each second hit swings late of its grid line */
        int32_t sw = v * 5 / 127;
        cv_rect(x, b + 1, 52, 1, GN_GUIDE);
        for (i = 0; i < 4; i++) {
            int32_t on = x + 2 + i * 13, off = on + 6;
            gn_vdash(off, y + 12, b, GN_GUIDE);
            cv_rect(on, y + 6, 2, b - y - 6, acc);
            gn_handle(on + 1, y + 5, acc);
            cv_rect(off + sw, y + 14, 2, b - y - 14, acc);
        }
        break;
    }
    case 4: {                                    /* human: a straight line that wanders */
        static const int8_t J[13] = {0, -8, 5, -11, 3, 9, -6, 12, -4, 7, -10, 4, 0};
        int32_t mid = y + 15, px = x + 2, py = mid;
        for (i = 0; i < 52; i += 4)
            cv_rect(x + i, mid + 4, 2, 1, GN_GUIDE);
        for (i = 1; i < 13; i++) {
            int32_t yy = mid + J[i] * v / 127;
            gn_ln(px, py, x + 2 + i * 4, yy, acc);
            px = x + 2 + i * 4;
            py = yy;
        }
        gn_handle(px, py, acc);
        break;
    }
    case 5: {                                    /* tension: the chord stacks up (triad, 7th, 9th, 11th...) */
        int n = 3 + v * 4 / 128;
        for (i = 0; i < 6; i++) {
            int32_t ly = b - 2 - i * 5, lx = x + 4 + (i & 1) * 14;
            if (i < n)
                cv_rect(lx, ly - 1, 30, 3, acc);
            else
                cv_rect(lx, ly, 30, 1, GN_GUIDE);
        }
        gn_handle(x + 4 + ((n - 1) & 1) * 14 + 30, b - 2 - (n - 1) * 5, acc);
        break;
    }
    case 6: {                                    /* complex: the beat divides into 8ths, then 16ths */
        int lvl = v < 43 ? 0 : v < 86 ? 1 : 2;
        cv_rect(x, b + 1, 52, 1, GN_GUIDE);
        for (i = 0; i < 16; i++) {
            int l = (i & 3) == 0 ? 0 : (i & 1) == 0 ? 1 : 2;
            int32_t h = l == 0 ? 22 : l == 1 ? 14 : 8;
            if (l <= lvl)
                cv_rect(x + 3 + i * 3, b + 1 - h, 2, h, acc);
            else
                cv_rect(x + 3 + i * 3, b - 2, 1, 3, GN_GUIDE);
        }
        gn_handle(x + 4 + (lvl == 0 ? 12 : lvl == 1 ? 14 : 15) * 3 - (lvl == 0 ? 0 : 0), b + 1 - (lvl == 0 ? 22 : lvl == 1 ? 14 : 8), acc);
        break;
    }
    default: {                                   /* evolve: the loop as written (grey) and as it drifts */
        int32_t k, gy = 0, ay = 0, pgy = 0, pay = 0, mid = y + 15;
        for (k = 0; k <= 48; k += 2) {
            int32_t ph = k * 256 / 26, amp = 10 + v * k * 4 / (127 * 48);
            gy = mid - 10 * gn_isin(ph) / 127;
            ay = mid - amp * gn_isin(ph + v * k * 90 / (127 * 48)) / 127;
            if (k) {
                cv_line(x + 1 + k, pgy, x + 3 + k, gy, GN_GUIDE);
                gn_ln(x + 1 + k, pay, x + 3 + k, ay, acc);
            }
            pgy = gy;
            pay = ay;
        }
        gn_handle(x + 51, ay, acc);
        break;
    }
    }
}

static void gn_macros_at(int32_t y0, int track)    /* GEN's, or (track) the selected track's own */
{
    static const char *const N[GX_NMACRO] = {"Density", "Sync", "Space", "Groove", "Human", "Tension", "Complex",
                                             "Evolve"};
    int i, hi = shift() ? 4 : 0;
    if (track)
        hi = 0;
    for (i = 0; i < 4; i++) {
        int m = hi + i, v = track ? gn_tmacro(m) : gn_macro(m);
        int own = track && (gn_owned(sel(), m) || ((gen.tmpend >> m & 1u) && gen.trole == sel()));
        int pend = track ? (gen.tmpend >> m & 1u) && gen.trole == sel() : gen.mpend >> m & 1u;
        int32_t x = 6 + i * 59;
        char t[6];
        cv_text(x, y0, &FONT_XS, N[m], GN_INK);
        gn_macro_pic(m, v, x, y0 + 17, track && !own ? GN_GUIDE : GN_INK);
        cv_text(x, y0 + 49, &FONT_XS, itoa_u((uint32_t)v, t), pend ? GN_INK : own ? GN_ACC : GN_MUTED);
        cv_text(x + 22, y0 + 49, &FONT_XS, i == 0 ? "K1" : i == 1 ? "K2" : i == 2 ? "K3" : "K4", GN_OFF);
    }
}

/* GLO held: what the white keys do now. The six parts as tiles (W9-W14; the part chosen filled, outlined while
   it waits for the next bar), and the mutate keys (W1-W5) */
static void gn_glo_layer(void)
{
    static const char *const K[6] = {"W9", "W10", "W11", "W12", "W13", "W14"};
    int k, waiting = s4p.on && s4p.next[0] != 0xFF;
    uint16_t acc = GN_INK;                       /* parts are the whole song's: neutral */
    pad(2, 174, 236, 64, GN_PANEL);
    cv_text(7, 176, &FONT_XS, "PARTS: the lit keys", GN_MUTED);
    text_r(232, 176, &FONT_XS, waiting ? "NEXT BAR" : "GLO + key", waiting ? acc : GN_MUTED);
    for (k = 0; k < (int)GX_NPART; k++) {
        int32_t x = 5 + k * 39;
        int on = gxb_ok && gxb_comp.part == k;
        const char *nm = gx_part_name((gx_genre_t)(gxb_ok ? gxb_comp.r[0].genre : gen.genre), (uint32_t)k);
        char t[12];
        scopy(t, nm, 12);
        fit(&FONT_XS, t, 34);
        if (on && !waiting)
            pad(x, 192, 36, 26, GN_INK);
        else
            frame(x, 192, 36, 26, on ? acc : GN_OFF);
        text_c(x + 18, 192, &FONT_XS, t, on && !waiting ? GN_BG : on ? acc : GN_INK);
        text_c(x + 18, 205, &FONT_XS, K[k], on && !waiting ? GN_PANEL : GN_MUTED);
    }
    cv_text(7, 221, &FONT_XS, "W1 rhythm W2 pitch W3 vel W4 len W5 all", GN_MUTED);
}

static void gn_info(void);
static void gn_strip(void)
{
    int s = sel(), i, ps = gn_playing_step(s), p = page();
    uint8_t vel[16], len[16];
    uint16_t acc = gn_c(s);
    if (shift()) {
        gn_glo_layer();
        return;
    }
    pad(2, 174, 236, 64, GN_PANEL);
    gn_bar(s, p, vel, len);
    for (i = 0; i < 16; i++) {
        int32_t x = 7 + i * 14 + (i >> 2) * 1, h;
        int ph = ps == p * 16 + i;
        uint16_t ring = ph ? GN_INK : vel[i] ? GN_MUTED : GN_OFF;
        if (ph)
            cv_rect(x + 4, 177, 4, 4, GN_INK);
        else
            cv_rect(x + 5, 178, 2, 2, i % 4 == 0 ? GN_MUTED : GN_OFF);
        frame(x, 184, 12, 26, ring);
        if (ph)
            frame(x + 1, 185, 10, 24, ring);
        if (vel[i]) {
            h = 2 + vel[i] * 19 / 127;
            cv_rect(x + 2, 208 - h, 8, h, acc);
            cv_rect(x, 213, len[i] > 1 ? 12 + (len[i] - 1) * 14 > 222 - x ? 222 - x : 12 + (len[i] - 1) * 14 : 6, 2, GN_INK);
        }
    }
    gn_info();
}

static void gn_info(void)                        /* the bottom line: the last message, or the chords */
{
    int s = sel(), p = page();
    uint16_t acc = gn_c(s);
    char t[28], n[8];
    if (ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0) {
        t[0] = 0;
        scat(t, ui.msg[0], 28);
        if (ui.msg[1][0]) {
            scat(t, "  ", 28);
            scat(t, ui.msg[1], 28);
        }
        fit(&FONT_XS, t, 228);
        cv_text(7, 219, &FONT_XS, t, GN_INK);
        return;
    }
    {
        int32_t x = cv_text(7, 219, &FONT_XS, gn_track_label(s), acc) + 8;
        int bar = gn_playing_step(GX_ROLE_CHORDS) >= 0 ? gn_playing_step(GX_ROLE_CHORDS) / 16 : p;
        if (!gxb_ok || !gxb_harm_ok) {
            t[0] = 0;
            scat(t, "ch ", 28);
            itoa_u((uint32_t)s4[s].ch + 1u, n);
            scat(t, n, 28);
            cv_text(x, 219, &FONT_XS, t, GN_MUTED);
        } else if (s == GX_ROLE_CHORDS) {        /* the progression: a numeral a chord, the one playing lit */
            int b;
            for (b = 0; b < 8 && x < 196; b++) {
                int b1 = b;
                if (b && gxb_harm.deg[b] == gxb_harm.deg[b - 1])   /* a chord held or repeated: one numeral */
                    continue;
                while (b1 + 1 < 8 && gxb_harm.deg[b1 + 1] == gxb_harm.deg[b])
                    b1++;
                gn_numeral(b, n);
                x = cv_text(x, 219, &FONT_XS, n, bar >= b && bar <= b1 ? acc : GN_MUTED) + 7;
            }
        } else {                                  /* the chord playing: VI Dmaj7 */
            char c[16];
            gn_numeral(bar, n);
            gn_chord_text(bar, c);
            x = cv_text(x, 219, &FONT_XS, n, GN_INK) + 4;
            cv_text(x, 219, &FONT_XS, c, GN_MUTED);
        }
    }
    t[0] = 0;
    scat(t, "bar ", 28);
    itoa_u((uint32_t)p + 1u, n);
    scat(t, n, 28);
    text_r(232, 219, &FONT_XS, t, GN_INK);
}


/* ---- the pages: the tabs and the song line (every page), then each page's own ---- */
static void gn_tabs(void)
{
    static const char *const T[3] = {"GEN", "TRACK", "ARRANGE"};
    char t[24], n[8];
    int i, bar = (gn_playing_step(0) >= 0 ? gn_playing_step(0) : page() * 16) / 16;
    int32_t x;
    cv_rect(0, 0, 240, 32, GN_CHROME);
    for (i = 0; i < 3; i++) {                    /* equal cells; the open one is the colour of what it opens */
        if (i == gen.pg)
            cv_rect(i * 80, 16, 80, 16, gen.pg == 1 ? GN_BG : GN_EDIT);
        if (i)
            cv_rect(i * 80, 16, 1, 16, GN_BG);
        text_c(i * 80 + 40, 16, &FONT_XS, T[i], i == gen.pg ? GN_ACC : GN_MUTED);
    }
    cv_rect(0, 16, 240, 1, GN_BG);
    scopy(t, gn_genre_label(gen.gpend ? gen.genre : (gxb_ok ? gxb_comp.r[0].genre : gen.genre)), 24);
    fit(&FONT_XS, t, 92);
    x = cv_text(4, 0, &FONT_XS, t, gen.gpend ? GN_MUTED : GN_INK);
    gn_key_text(t, 0);
    x = cv_text(x + 8, 0, &FONT_XS, t, GN_INK);
    if (gxb_ok) {
        scopy(t, gx_part_name((gx_genre_t)gxb_comp.r[0].genre, gxb_comp.part), 24);
        fit(&FONT_XS, t, 184 - (x + 8));
        cv_text(x + 8, 0, &FONT_XS, t, s4p.on && s4p.next[0] != 0xFF ? GN_ACC : GN_INK);
    }
    itoa_u(s4g.tempo / 10u, n);
    text_r(210, 0, &FONT_XS, n, gn_tempo_out() ? GN_INK : GN_MUTED);
    for (i = 0; i < 8; i++) {                    /* the loop's bars, the one shown filled */
        int32_t bx = 213 + (i & 3) * 6, by = 3 + (i >> 2) * 6;
        if (i == bar)
            cv_rect(bx, by, 5, 5, GN_ACC);
        else
            frame(bx, by, 5, 5, GN_GUIDE);
    }
}

static int32_t gn_col(int i) { return 18 + i * 13 + (i >> 2); }   /* a step's column: 16 across, a gap a beat */

/* GEN: the four tracks, this bar: a square for the track (filled: selected), a bar a hit (taller: louder) */
static void gn_song_rows(void)
{
    int r, i;
    cv_rect(0, 32, 240, 52, GN_EDIT);
    for (r = 0; r < 4; r++) {
        int32_t y = 32 + r * 13;
        int on = r == sel(), muted = s4[r].mute;
        uint16_t c = muted ? GN_OFF : on ? GN_ACC : gn_dim(r);
        uint8_t vel[16], len[16];
        int ps = gn_playing_step(r), bar = (ps >= 0 ? ps : page() * 16) / 16;
        if (on)
            cv_rect(4, y + 3, 7, 7, c);
        else
            frame(4, y + 3, 7, 7, c);
        gn_bar(r, bar, vel, len);
        for (i = 0; i < 16; i++) {
            int32_t x = gn_col(i), h;
            if (!vel[i]) {
                cv_rect(x + 5, y + 6, 2, 2, i % 4 ? GN_LINE : GN_OFF);
                continue;
            }
            h = 3 + vel[i] * 7 / 127;
            cv_rect(x, y + 11 - h, 11, h, c);
            if (len[i] > 1) {
                int32_t w = (int32_t)(len[i] - 1) * 13;
                if (x + 11 + w > 230)
                    w = 230 - x - 11;
                if (w > 0)
                    cv_rect(x + 11, y + 10, w, 1, c);
            }
        }
        if (gn_locked(r))
            gn_lock_icon(232, y + 2, on ? GN_INK : GN_MUTED);
    }
}
static void gn_gen_line(void)                    /* GEN: the idea; FX rolls a new one */
{
    char t[12];
    cv_text(4, 84, &FONT_XS, "GEN", GN_MUTED);
    gn_seed_text(t);
    cv_text(36, 84, &FONT_XS, gxb_ok || gen.spend ? t : "-", gen.spend ? GN_INK : GN_ACC);
    text_r(236, 84, &FONT_XS, "FX new idea", GN_MUTED);
}

/* TRACK: the track tabs (black keys 1-4), then the editor */
static void gn_track_tabs(void)
{
    int r;
    cv_rect(0, 32, 240, 16, GN_CHROME);
    for (r = 0; r < 4; r++) {
        int32_t x = r * 60;
        int on = r == sel();
        char t[10];
        cv_rect(x + (r ? 1 : 0), 32, r ? 59 : 60, 16, on ? GN_EDIT : GN_BG);
        scopy(t, gn_track_label(r), 10);
        fit(&FONT_XS, t, 54);
        text_c(x + 30, 32, &FONT_XS, t, s4[r].mute ? GN_OFF : on ? GN_ACC : GN_MUTED);
    }
}
static void gn_lanes(int bar, uint16_t *hit)     /* the drums' lanes this bar: a bit a step */
{
    const s4_seq_t *q = &s4[GX_ROLE_DRUMS];
    int k, l;
    for (l = 0; l < (int)GX_NLANE; l++)
        hit[l] = 0;
    if (q->mode != M_POLY)
        return;
    for (k = 0; k < q->d.poly.n; k++) {
        const s4_pnote_t *n = &q->d.poly.note[k];
        int st = n->step - bar * 16;
        if (st >= 0 && st < 16 && n->deg >= 0 && n->deg < (int)GX_NLANE)
            hit[n->deg] |= (uint16_t)(1u << st);
    }
}
static void gn_editor(void)
{
    int s = sel(), i, p = page();
    cv_rect(0, 48, 240, 52, GN_EDIT);
    if (s == GX_ROLE_DRUMS && s4[s].mode == M_POLY) {   /* a row a drum sound: kick at the top */
        uint16_t hit[GX_NLANE];
        int l;
        gn_lanes(p, hit);
        for (l = 0; l < (int)GX_NLANE; l++)
            for (i = 0; i < 16; i++) {
                int32_t x = gn_col(i), y = 50 + l * 5;
                if (hit[l] >> i & 1u)
                    cv_rect(x, y, 11, 4, GN_ACC);
                else
                    cv_rect(x + 5, y + 1, 1, 2, i % 4 ? GN_LINE : GN_OFF);
            }
        return;
    }
    {                                            /* a pitched track: its steps, velocity as height */
        uint8_t vel[16], len[16];
        gn_bar(s, p, vel, len);
        for (i = 0; i < 16; i++) {
            int32_t x = gn_col(i), h;
            frame(x, 51, 11, 42, vel[i] ? GN_ACC : i % 4 ? GN_LINE : GN_OFF);
            if (vel[i]) {
                h = 3 + vel[i] * 38 / 127;
                cv_rect(x + 1, 92 - h, 9, h, GN_ACC);
                cv_rect(x, 95, len[i] > 1 ? (gn_col(i + len[i] - 1 > 15 ? 15 : i + len[i] - 1) + 11 - x) : 5, 2, GN_INK);
            }
        }
    }
}
static void gn_track_strip(void)                 /* MUTATE: how many used, the keys; then the bottom line */
{
    int s = sel(), n = gxb_ok ? gxb_comp.r[s].mut_n : 0, i;
    int32_t x;
    if (shift()) {
        gn_glo_layer();
        return;
    }
    pad(2, 174, 236, 64, GN_PANEL);
    x = cv_text(7, 176, &FONT_XS, "MUTATE", GN_MUTED) + 8;
    for (i = 0; i < 4; i++)
        if (i < n)
            cv_rect(x + i * 9, 181, 6, 6, GN_ACC);
        else
            frame(x + i * 9, 181, 6, 6, GN_OFF);
    text_r(232, 176, &FONT_XS, s4[s].mute ? "MUTED" : gn_locked(s) ? "LOCKED" : "", GN_ACC);
    cv_text(7, 196, &FONT_XS, "SEL all  GLO+FX re-roll", GN_MUTED);
    gn_info();
}

/* ARRANGE: the energy of the six parts, the parts (W1-W6 queue one), the loop */
static const uint8_t GN_ENERGY[6] = {30, 70, 75, 35, 60, 100};
static int32_t gn_ex(int k) { return 24 + k * 38; }
static int32_t gn_ey(int k) { return 93 - GN_ENERGY[k] * 42 / 100; }
static void gn_energy(void)
{
    int k, p = gxb_ok ? gxb_comp.part : 1, waiting = s4p.on && s4p.next[0] != 0xFF;
    cv_rect(0, 32, 240, 68, GN_EDIT);
    cv_text(4, 33, &FONT_XS, "ENERGY", GN_MUTED);
    cv_rect(4, 97, 232, 1, GN_LINE);
    for (k = 0; k < 5; k++)
        gn_ln(gn_ex(k), gn_ey(k), gn_ex(k + 1), gn_ey(k + 1), GN_OFF);
    for (k = 0; k < 6; k++) {
        int32_t x = gn_ex(k) - 3, y = gn_ey(k) - 3;
        if (k == p && waiting) {
            cv_rect(x, y, 7, 7, GN_EDIT);
            frame(x, y, 7, 7, GN_ACC);
        } else
            cv_rect(x, y, 7, 7, k == p ? GN_ACC : GN_GUIDE);
    }
}
static void gn_part_tiles(void)
{
    static const char *const K[6] = {"W1", "W2", "W3", "W4", "W5", "W6"};
    int k, waiting = s4p.on && s4p.next[0] != 0xFF;
    cv_rect(0, 100, 240, 72, GN_EDIT);
    for (k = 0; k < (int)GX_NPART; k++) {
        int32_t x = 4 + (k % 3) * 78, y = 104 + (k / 3) * 32;
        int on = gxb_ok && gxb_comp.part == k, full = on && !waiting;
        char t[12];
        cv_rect(x, y, 76, 30, full ? GN_ACC : GN_BG);
        if (on && waiting)
            frame(x, y, 76, 30, GN_ACC);
        scopy(t, gx_part_name((gx_genre_t)(gxb_ok ? gxb_comp.r[0].genre : gen.genre), (uint32_t)k), 12);
        fit(&FONT_XS, t, 50);
        cv_text(x + 4, y + 7, &FONT_XS, t, full ? GN_BG : GN_INK);
        text_r(x + 72, y + 7, &FONT_XS, K[k], full ? GN_PANEL : GN_MUTED);
    }
}
static void gn_arr_strip(void)
{
    int b, bar = gn_playing_step(0) >= 0 ? gn_playing_step(0) / 16 : -1;
    if (shift()) {
        gn_glo_layer();
        return;
    }
    pad(2, 174, 236, 64, GN_PANEL);
    cv_text(7, 176, &FONT_XS, "W1-W6: queue a part", GN_MUTED);
    for (b = 0; b < 8; b++)                      /* the loop: the bars played, the one playing */
        cv_rect(7 + b * 29, 199, 26, 6, bar < 0 ? GN_LINE : b < bar ? GN_ACC : b == bar ? GN_INK : GN_LINE);
    gn_info();
}

static void gn_band_a(void)
{
    gn_tabs();
    if (gen.pg == 0) {
        gn_song_rows();
        gn_gen_line();
    } else if (gen.pg == 1) {
        gn_track_tabs();
        gn_editor();
    } else
        gn_energy();
}
static void gn_band_b(void)
{
    if (gen.pg == 2)
        gn_part_tiles();
    else
        gn_macros_at(104, gen.pg == 1 && !shift());
}
static void gn_band_c(void)
{
    if (gen.pg == 0)
        gn_strip();
    else if (gen.pg == 1)
        gn_track_strip();
    else
        gn_arr_strip();
}

static uint32_t gn_sig_bar(uint32_t h, int s, int bar)
{
    uint8_t vel[16], len[16];
    int i;
    gn_bar(s, bar, vel, len);
    for (i = 0; i < 16; i++)
        h = hash(h, (uint32_t)vel[i] << 8 | len[i]);
    return h;
}

static void gn_band(int32_t y0, int32_t h, void (*paint)(void))
{
    cv_begin(240, (uint32_t)h, GN_BG);
    cv_oy = -y0;                                 /* draw in screen coordinates */
    paint();
    cv_blit(0, (uint32_t)y0);
}

static void draw_gen(void)
{
    uint32_t h;
    int r, m, msg = ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0;
    if (!gen.drawn) {
        gen.sig[0] = gen.sig[1] = gen.sig[2] = 0;
        gen.drawn = 1;
    }
    /* the top: the tabs, the song line, and the page's own (the tracks; the editor; the energy) */
    h = hash(hash(hash(1u, sel()), s4g.tempo), (uint32_t)(gn_playing_step(0) / 16) + 7u * (uint32_t)page());
    h = hash(hash(h, gen.gpend ? gen.genre : 0xFFu), gxb_ok ? (uint32_t)gxb_comp.part << 8 | gxb_comp.lock : 0x1234u);
    h = hash(hash(h, (uint32_t)gn_key() << 8 | (uint32_t)gn_tempo_out()), (uint32_t)gen.pg << 4 | (uint32_t)(s4p.on && s4p.next[0] != 0xFF));
    h = hash(hash(h, gen.seed), gen.spend | (uint32_t)gxb_ok << 1);
    for (r = 0; r < 4; r++) {
        int ps = gn_playing_step(r);
        h = hash(hash(h, s4[r].mute), gxb_ok ? gxb_comp.r[r].mut_n | (uint32_t)gxb_comp.r[r].genre << 4 : 0u);
        h = gn_sig_bar(h, r, (ps >= 0 ? ps : page() * 16) / 16);
    }
    if (gen.pg == 1) {                            /* the editor: the bar shown, the drums' lanes */
        uint16_t hit[GX_NLANE];
        h = gn_sig_bar(h, sel(), page());
        gn_lanes(page(), hit);
        for (r = 0; r < (int)GX_NLANE; r++)
            h = hash(h, hit[r]);
    }
    if (h != gen.sig[0]) {
        gen.sig[0] = h;
        gn_band(0, 100, gn_band_a);
    }
    /* the middle: the macros (GEN's, or the track's own); the parts */
    h = hash(hash(hash(2u, sel()), (uint32_t)shift() << 8 | gen.mpend), (uint32_t)gen.pg << 8 | gen.tmpend);
    for (m = 0; m < (int)GX_NMACRO; m++)
        h = hash(h, (uint32_t)gn_macro(m) | (uint32_t)(m < 4 ? gn_tmacro(m) : 0) << 8 | (uint32_t)gn_owned(sel(), m) << 16);
    h = hash(hash(h, gxb_ok ? gxb_comp.part : 9u), (uint32_t)(s4p.on && s4p.next[0] != 0xFF) | (uint32_t)gen.trole << 1);
    if (gen.pg == 2)
        h = hash(h, gxb_ok ? gxb_comp.r[0].genre : gen.genre);
    if (h != gen.sig[1]) {
        gen.sig[1] = h;
        gn_band(100, 72, gn_band_b);
    }
    /* the bottom: the steps; MUTATE; the loop. And the last message */
    h = hash(hash(hash(3u, sel()), page()), (uint32_t)(gn_playing_step(sel()) + 1));
    h = gn_sig_bar(h, sel(), page());
    h = hash(h, msg ? hash_s(hash_s(5u, ui.msg[0]), ui.msg[1]) : 0u);
    h = hash(hash(h, s4[sel()].ch), (uint32_t)gen.pg << 4 | (uint32_t)s4[sel()].mute << 2 | (uint32_t)gn_locked(sel()));
    h = hash(h, (uint32_t)shift() << 9 | (uint32_t)(gxb_ok ? gxb_comp.part : 7u) << 1 | (uint32_t)(s4p.on && s4p.next[0] != 0xFF));
    h = hash(h, gxb_ok ? gxb_comp.r[sel()].mut_n : 0u);
    if (gen.pg == 2)
        h = hash(h, (uint32_t)(gn_playing_step(0) / 16 + 2));
    if (gxb_ok && gxb_harm_ok) {                  /* the chords shown: the key, the progression, the bar playing */
        int b;
        h = hash(hash(h, gxb_comp.key), (uint32_t)(gn_playing_step(GX_ROLE_CHORDS) / 16 + 2));
        for (b = 0; b < 8; b++)
            h = hash(h, (uint32_t)gxb_harm.deg[b] << 16 | (uint32_t)gxb_harm.pcs[b]);
    }
    if (h != gen.sig[2]) {
        gen.sig[2] = h;
        gn_band(172, 68, gn_band_c);
    }
}

/* ------------------------------------------------------------------------------------ input --- */
static int gn_result(int res, const char *what)
{
    if (res < 0)
        say(what, "DID NOT FIT");
    return res;
}
/* a fresh seed: the sequencer's generator starts the same at every power-on, so the moment of the press (ms
   since power-on) is mixed in */
static uint32_t gn_fresh_seed(void) { return (s4_rand() ^ (plat_ms() * 2654435761u)) % 10000u; }
static int gn_ensure(void)                       /* the first idea, in the genre picked, from a fresh seed */
{
    if (gxb_ok)
        return 0;
    if (!gen.spend)
        gen.seed = gn_fresh_seed();
    return gn_result(gxb_new(gen.genre, gen.seed), "NEW IDEA") < 0 ? -1 : 0;
}
static void gn_seed_text(char *t)
{
    char n[12];
    scopy(t, "#", 12);
    itoa_u(gen.seed % 10000u, n);
    scat(t, n, 12);
}
static void gn_new_idea(void)
{
    char t[12];
    gen.seed = gn_fresh_seed();
    gen.spend = 0;
    if (!gxb_ok)
        gn_ensure();
    else
        gn_result(gxb_compose(gen.seed), "NEW IDEA");
    gn_seed_text(t);
    say("NEW IDEA", t);
}
static void gn_mutate(int mask)
{
    static const char *const A[4] = {"RHYTHM", "PITCH", "VELOCITY", "LENGTH"};
    int r = sel();
    if (gn_ensure())
        return;
    if (gn_locked(r)) {
        say(gn_track_label(r), "LOCKED");
        return;
    }
    if (gxb_comp.r[r].mut_n >= GX_MAXMUT) {
        say("4 MUTATIONS", "EDIT UNDOES");
        return;
    }
    if (gn_result(gxb_mutate(r, mask, 40, s4_rand()), "MUTATE") >= 0)
        say("MUTATE", mask == (int)GX_MUT_ALL ? "ALL" : mask == 1 ? A[0] : mask == 2 ? A[1] : mask == 4 ? A[2] : A[3]);
}

static void gn_reroll(void)                      /* the selected track, afresh (GLO + FX) */
{
    int r = sel();
    if (gn_ensure())
        return;
    if (gn_locked(r))
        say(gn_track_label(r), "LOCKED");
    else if (gn_result(gxb_reseed(r, s4_rand()), "RE-ROLL") >= 0)
        say("RE-ROLL", gn_track_label(r));
}
static void gn_part(int k)
{
    if (k >= 0 && k < (int)GX_NPART && !gn_ensure() && gn_result(gxb_part(k), "PART") >= 0)
        say(gx_part_name((gx_genre_t)gxb_comp.r[0].genre, (uint32_t)k), s4p.on ? "NEXT" : "");
}
static void gn_set_mute(int r, int on)
{
    if (!!s4[r].mute != !!on)
        mute_seq(r, 0);
    say(gn_track_label(r), on ? "MUTED" : "ON");
}
static void gn_set_lock(int r, int on)
{
    if (!gn_ensure() && gn_locked(r) != !!on && gn_result(gxb_lock(r, on), "LOCK") >= 0)
        say(gn_track_label(r), on ? "LOCKED" : "UNLOCKED");
}
/* MIDI on GX-1's channel (16): the panel from a controller. docs/GX1_MIDI.md has the map; the player uses the same.
 *   CC 20-27   the eight macros (Density, Sync, Space, Groove, Human, Tension, Complex, Evolve), applied as K1-K4 are
 *   CC 52-55   mute drums, sub, chords, upper (64 and up: muted)   CC 56-59 lock them (64 and up: locked)
 *   CC 60-63   select a track   CC 85 new idea   86 mutate   87 re-roll   88 undo   89 redo   90 the next page
 *              (these on a press: 64 and up; SEQ-4's transport CCs 105-107 are left alone)
 *   notes      36 new idea  37 mutate  38 re-roll  39 undo  40-43 select  44-47 mute (toggle)  48-53 the six parts
 *              54 redo  55 the next page
 *   program change 0-5: the six parts */
static void gn_act(int a)                        /* the actions the CCs and the notes share */
{
    switch (a) {
    case 0: gn_new_idea(); break;
    case 1: gn_mutate((int)GX_MUT_ALL); break;
    case 2: gn_reroll(); break;
    case 3: say(gxb_undo() >= 0 ? "UNDO" : "NOTHING TO UNDO", 0); break;
    case 4: say(gxb_redo() >= 0 ? "REDO" : "NOTHING TO REDO", 0); break;
    case 5: gen.pg = (uint8_t)((gen.pg + 1) % 3); gen.drawn = 0; break;
    default: break;
    }
}
static void gn_midi(int st, int d1, int d2)
{
    int kind = st & 0xF0, on = d2 >= 64;
    if (kind == 0x90 && d2 == 0)
        kind = 0x80;
    if (kind == 0xB0) {
        if (d1 >= 20 && d1 <= 27) {              /* a macro: as the knob, applied when it rests */
            int m = d1 - 20;
            gen.mac[m] = (uint8_t)d2;
            gen.mpend |= (uint8_t)(1u << m);
            gen.mpend_t = plat_ms();
        } else if (d1 >= 52 && d1 <= 55)
            gn_set_mute(d1 - 52, on);
        else if (d1 >= 56 && d1 <= 59)
            gn_set_lock(d1 - 56, on);
        else if (on && d1 >= 60 && d1 <= 63) {
            set_sel(d1 - 60);
            say(gn_track_label(d1 - 60), gn_locked(d1 - 60) ? "LOCKED" : "");
        } else if (on && d1 >= 85 && d1 <= 90)
            gn_act(d1 - 85);
    } else if (kind == 0x90) {
        if (d1 >= 36 && d1 <= 39)
            gn_act(d1 - 36);
        else if (d1 >= 40 && d1 <= 43) {
            set_sel(d1 - 40);
            say(gn_track_label(d1 - 40), gn_locked(d1 - 40) ? "LOCKED" : "");
        } else if (d1 >= 44 && d1 <= 47)
            gn_set_mute(d1 - 44, !s4[d1 - 44].mute);
        else if (d1 >= 48 && d1 <= 53)
            gn_part(d1 - 48);
        else if (d1 == 54)
            gn_act(4);
        else if (d1 == 55)
            gn_act(5);
    } else if (kind == 0xC0)
        gn_part(d1);
}
static void gn_midi_service(void)
{
    int n = 0;
    while (s4_gxq_r != s4_gxq_w && n++ < 16) {
        uint32_t x = s4_gxq[s4_gxq_r % S4_GXQ];
        s4_gxq_r++;
        gn_midi((int)(x & 0xFFu), (int)(x >> 8 & 0x7Fu), (int)(x >> 16 & 0x7Fu));
    }
}

static void gen_key_down(int k)
{
    int w = white_of(k), b = black_of(k);
    if (w >= 0) {
        if (gen.pg == 2 && !shift()) {           /* ARRANGE: W1-W6 queue a part; the other keys rest */
            if (w < (int)GX_NPART && !gn_ensure() && gn_result(gxb_part(w), "PART") >= 0)
                say(gx_part_name((gx_genre_t)gxb_comp.r[0].genre, (uint32_t)w), s4p.on ? "NEXT" : "");
            gen.wshift |= (uint16_t)(1u << w);   /* (its release is ours) */
            return;
        }
        if (shift()) {
            ui.btn_used |= 1u << B_GLO;
            gen.wshift |= (uint16_t)(1u << w);
            if (w < 4)
                gn_mutate(1 << w);
            else if (w == 4)
                gn_mutate((int)GX_MUT_ALL);
            else if (w >= 8 && w < 8 + (int)GX_NPART) {
                if (!gn_ensure() && gn_result(gxb_part(w - 8), "PART") >= 0)
                    say(gx_part_name((gx_genre_t)gxb_comp.r[0].genre, (uint32_t)(w - 8)), s4p.on ? "NEXT" : "");
            }
            return;
        }
        grid_white_down(w);
        return;
    }
    if (b >= 0 && b < 4) {
        set_sel(b);
        say(gn_track_label(b), gn_locked(b) ? "LOCKED" : "");
    } else if (b >= 4 && b < 8) {
        int was = s4[b - 4].mute;                /* (the engine applies it on its next tick) */
        mute_seq(b - 4, 0);
        say(gn_track_label(b - 4), was ? "ON" : "MUTED");
    } else if (b == BK_PREV || b == BK_NEXT) {
        set_page(page() + (b == BK_NEXT ? 1 : -1));
    } else if (b == BK_MUTE) {
        mute_seq(sel(), 0);
    }
}
static void gen_key_up(int k)
{
    int w = white_of(k);
    if (w < 0)
        return;
    if (gen.wshift >> w & 1u) {
        gen.wshift &= (uint16_t)~(1u << w);
        return;
    }
    grid_white_up(w);
}

static int gen_btn_down(int b)
{
    if (b == B_OCTDN || b == B_OCTUP) {          /* the key, a semitone */
        char t[16];
        ui.btn_used |= 1u << b;
        if (!gn_ensure() && gn_result(gxb_key(gxb_comp.key + (b == B_OCTUP ? 1 : -1)), "KEY") >= 0) {
            gn_key_text(t, 1);
            say("KEY", t);
        }
        return 1;
    }
    if (b == B_FX && shift()) {                  /* GLO + FX: re-roll the selected track */
        ui.btn_used |= 1u << b | 1u << B_GLO;
        gn_reroll();
        return 1;
    }
    if (b == B_EDIT && shift()) {
        say(gxb_redo() >= 0 ? "REDO" : "NOTHING TO REDO", 0);
        ui.btn_used |= 1u << b | 1u << B_GLO;
        return 1;
    }
    return 0;
}
static int gen_btn_tap(int b)
{
    int r = sel();
    switch (b) {
    case B_FX: gn_new_idea(); return 1;
    case B_SEL: gn_mutate((int)GX_MUT_ALL); return 1;
    case B_ENV:
        if (!gn_ensure() && gn_result(gxb_lock(r, !gn_locked(r)), "LOCK") >= 0)
            say(gn_track_label(r), gn_locked(r) ? "LOCKED" : "UNLOCKED");
        return 1;
    case B_EDIT: say(gxb_undo() >= 0 ? "UNDO" : "NOTHING TO UNDO", 0); return 1;
    case B_SEQ: set_view(V_GRID); return 1;      /* the grid, to edit by hand (HOME comes back) */
    case B_HOME:                                 /* the next page; twice: the patterns */
        if (plat_ms() - ui.up_t[B_HOME] < DTAP_MS) {
            gen.pg = (uint8_t)((gen.pg + 2) % 3);    /* (the first tap's step undone) */
            set_view(V_PATS);
        } else {
            gen.pg = (uint8_t)((gen.pg + 1) % 3);
            gen.drawn = 0;
        }
        return 1;
    default: return 0;
    }
}

static void gen_enc(int role, int32_t e)
{
    int d = e > 0 ? 1 : -1;
    uint32_t now = plat_ms();
    char t[12];
    switch (role) {
    case EN_ALGO: {                              /* the family; GLO: the style in it */
        int f = gn_family_of(gen.genre), nf = (int)gx_nfamily, n;
        if (shift()) {
            ui.btn_used |= 1u << B_GLO;
            n = gx_families[f].n;
            gen.genre = gx_families[f].g[(gn_style_index(f, gen.genre) + n + d) % n];
        } else {
            gen.fam_style[f] = (uint8_t)(gn_style_index(f, gen.genre) + 1);
            f = (f + nf + d) % nf;
            gen.genre = gx_families[f].g[gen.fam_style[f] ? gen.fam_style[f] - 1 : 0];
        }
        gen.gpend = 1;
        gen.gpend_t = now;
        say(gx_families[gn_family_of(gen.genre)].name, gn_genre_label(gen.genre));
        break;
    }
    case EN_PRESET:
        gen.seed = (gen.seed + 10000u + (uint32_t)d) % 10000u;
        gen.spend = 1;
        gen.spend_t = now;
        gn_seed_text(t);
        say("IDEA", t);
        break;
    case EN_SELECT: {                            /* the tempo (any; the genre's range is marked) */
        int v = clampi((int)s4g.tempo + (int)accel(EN_SELECT, e, 100) * 10, 600, 2000);
        const gx_genre_prof_t *p = gn_prof();
        s4g.tempo = (uint16_t)v;
        if (gxb_ok)
            gxb_comp.bpm_x10 = (uint16_t)v;
        dirty_g();
        itoa_u((uint32_t)v / 10u, t);
        scat(t, " BPM", 12);
        if (p && gn_tempo_out()) {
            char r[24], n2[6];
            scopy(r, "OUTSIDE ", 24);
            itoa_u(p->tempo_min_x10 / 10u, n2);
            scat(r, n2, 24);
            scat(r, "-", 24);
            itoa_u(p->tempo_max_x10 / 10u, n2);
            scat(r, n2, 24);
            say(t, r);
        } else
            say("TEMPO", t);
        break;
    }
    default: break;
    }
}
static void gen_knob(int k, int32_t e)
{
    int m = k + (shift() ? 4 : 0), v = gn_macro(m);
    if (gen.pg == 1 && !shift()) {               /* TRACK: the selected track's own */
        if (gen.tmpend && gen.trole != sel()) {  /* another track's still waiting: it lands first */
            int i;
            if (!gn_ensure())
                for (i = 0; i < 4; i++)
                    if (gen.tmpend >> i & 1u)
                        gn_result(gxb_macro_role(gen.trole, i, gen.tmac[i]), "MACRO");
            gen.tmpend = 0;
        }
        v = gn_tmacro(k);
        gen.trole = (uint8_t)sel();
        gen.tmac[k] = (uint8_t)clampi(v + (int)accel(EN_K1 + k, e, 127), 0, 127);
        gen.tmpend |= (uint8_t)(1u << k);
        gen.tmpend_t = plat_ms();
        return;
    }
    if (shift())
        ui.btn_used |= 1u << B_GLO;
    gen.mac[m] = (uint8_t)clampi(v + (int)accel(EN_K1 + k, e, 127), 0, 127);
    gen.mpend |= (uint8_t)(1u << m);
    gen.mpend_t = plat_ms();
}

static void gen_service(void)
{
    uint32_t now = plat_ms();
    int m;
    gn_midi_service();                           /* a controller on channel 16 */
    if (gen.gpend && now - gen.gpend_t > 700u) {
        gen.gpend = 0;
        gen.spend = 0;
        gen.seed = gn_fresh_seed();             /* a new genre: a fresh starting point */
        if (gn_result(gxb_new(gen.genre, gen.seed), "NEW IDEA") >= 0)
            say(gn_genre_label(gen.genre), "NEW IDEA");
    }
    if (gen.spend && now - gen.spend_t > 450u) {
        char t[12];
        gen.spend = 0;
        if (!gxb_ok)
            gn_ensure();
        else
            gn_result(gxb_compose(gen.seed), "IDEA");
        gn_seed_text(t);
        say("IDEA", t);
    }
    if (gen.tmpend && now - gen.tmpend_t > 250u) {
        if (!gn_ensure())
            for (m = 0; m < 4; m++)
                if (gen.tmpend >> m & 1u)
                    gn_result(gxb_macro_role(gen.trole, m, gen.tmac[m]), "MACRO");
        gen.tmpend = 0;
    }
    if (gen.mpend && now - gen.mpend_t > 250u) {
        if (!gn_ensure())
            for (m = 0; m < (int)GX_NMACRO; m++)
                if (gen.mpend >> m & 1u)
                    gn_result(gxb_macro(m, gen.mac[m]), "MACRO");
        gen.mpend = 0;
    }
    if (ui.view == V_GEN && s4p.on && !ui.held)  /* the page follows the bar playing */
        set_page(s4p.step[sel()][0] / 16);
}

static void gen_leds(uint32_t *bp, uint32_t *kp)
{
    uint32_t k = *kp, b = *bp;
    int i;
    for (i = 0; i < 8; i++)
        k &= ~(1u << BLACK_KEY[i]);
    k |= 1u << BLACK_KEY[sel()];
    for (i = 0; i < 4; i++)
        if (s4[i].mute)
            k |= 1u << BLACK_KEY[4 + i];
    if (gen.pg == 2 && !shift()) {               /* ARRANGE: W1-W6 the parts, the one chosen blinking */
        for (i = 0; i < 16; i++)
            k &= ~(1u << WHITE_KEY[i]);
        for (i = 0; i < (int)GX_NPART; i++)
            if (!(gxb_ok && i == gxb_comp.part) || blink())
                k |= 1u << WHITE_KEY[i];
    }
    if (shift()) {                               /* the second layer: mutate, the parts */
        for (i = 0; i < 16; i++)
            k &= ~(1u << WHITE_KEY[i]);
        for (i = 0; i < 5; i++)                  /* W1-W5: mutate */
            k |= 1u << WHITE_KEY[i];
        for (i = 0; i < (int)GX_NPART; i++)      /* W9-W14: the six parts lit, the one chosen blinking */
            if (!(gxb_ok && i == gxb_comp.part) || blink())
                k |= 1u << WHITE_KEY[8 + i];
    }
    if (gn_locked(sel()))
        b |= 1u << B_ENV;
    *kp = k;
    *bp = b;
}

static void gen_init(void)
{
    gen.genre = GX_GENRE_BREAKS;
    s4_gx_ch = 16;                               /* GX-1 listens on channel 16 (docs/GX1_MIDI.md) */
    gen.seed = 1;
}

static int gen_left(void)                        /* the page was on screen: it is not now */
{
    int was = gen.drawn;
    gen.drawn = 0;
    return was;
}
