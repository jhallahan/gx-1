/* SPDX-License-Identifier: GPL-3.0-only */
/* GX-1: the GEN page (part of ui.c, after ui_draw.c). The design's "Direction E": near-black, outlines first,
 * every track in its own colour, the one selected at full strength and the rest dimmed but readable.
 *
 *   y   0 ..  19  the genre, key and tempo; the part playing and the bar
 *   y  20 .. 99   the four tracks: their colour, name, this bar's hits (height = velocity), lock, mutations
 *   y 100 .. 171  K1-K4: the macros as small pictures (GLO held: the second four)
 *   y 172 .. 239  the selected track's 16 steps (the white keys): playhead dot, velocity fill, length bar
 *
 * Keys and controls on this page:
 *   white keys        the selected track's steps (as on the grid: tap on / off, hold for its values)
 *   GLO + W1-W4, W5   mutate the selected track: rhythm, pitch, velocity, length; all
 *   GLO + W9-W14      the six parts (each genre names its own)
 *   black 1-4 · 5-8   select drums, sub, chords, upper · mute them
 *   black 9, 10       the page (bar) back and on
 *   K1-K4             density, syncopation, space, groove (GLO: humanise, tension, complexity, evolution)
 *   ALGORITHM         the genre (a new idea in it when the knob rests)
 *   PRESETS           the seed (a new idea from it when the knob rests; locked tracks stay)
 *   SELECT            the tempo (the genre's range is marked, not enforced: ADR-028)
 *   OCT- / OCT+       the key, a semitone (everything pitched moves; the drums stay)
 *   FX new idea (GLO: re-roll the selected track) · SEL mutate · ENV lock · EDIT undo (GLO: redo)
 *   HOME the grid (and back) */

#define GN_BG RGB(17, 17, 20)
#define GN_PANEL RGB(30, 30, 35)
#define GN_LINE RGB(46, 46, 52)
#define GN_OFF RGB(58, 58, 66)
#define GN_MUTED RGB(139, 139, 147)
#define GN_INK RGB(236, 236, 240)
static const uint16_t GN_TRACK[4] = {RGB(242, 180, 78), RGB(255, 111, 97), RGB(95, 214, 160), RGB(160, 140, 255)};

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
} gen;

static uint16_t gn_c(int r) { return GN_TRACK[r & 3]; }
static uint16_t gn_dim(int r) { return mix(GN_TRACK[r & 3], GN_BG, 120); }

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
static int gn_macro(int m)
{
    if (gen.mpend >> m & 1u)
        return gen.mac[m];
    return gxb_ok ? gxb_comp.r[0].macro[m] : 64;
}
static int gn_locked(int r) { return gxb_ok && (gxb_comp.lock >> r & 1u); }

/* ---- the key, the chords, the tempo range (ADR-028) ---- */
static const char *const GN_NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const uint8_t GN_SCALE[3][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}};
static const gx_genre_prof_t *gn_prof(void) { return gx_profile((gx_genre_t)(gxb_ok ? gxb_comp.r[GX_ROLE_CHORDS].genre : gen.genre)); }
static int gn_scale(void) { const gx_genre_prof_t *p = gn_prof(); return p && p->harm.scale < 3 ? p->harm.scale : 0; }
static int gn_key(void) { const gx_genre_prof_t *p = gn_prof(); return gxb_ok ? gxb_comp.key : p ? p->harm.key : 0; }
static void gn_key_text(char *t, int full)        /* "F#m" / "F# minor" */
{
    static const char *const S[3] = {"m", "dor", "phr"}, *const F[3] = {" minor", " dorian", " phrygian"};
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

static void gn_header(void)
{
    char t[24], n[8];
    int32_t x;
    int bar = (gn_playing_step(0) >= 0 ? gn_playing_step(0) : page() * 16) / 16;
    x = cv_text(4, 2, &FONT_B, gn_genre_label(gen.gpend ? gen.genre : (gxb_ok ? gxb_comp.r[0].genre : gen.genre)),
                gen.gpend ? GN_MUTED : GN_INK);
    gn_key_text(t, 0);
    x = cv_text(x + 6, 3, &FONT_XS, t, GN_MUTED);
    itoa_u(s4g.tempo / 10u, n);
    {
        int32_t x1 = cv_text(x + 5, 3, &FONT_XS, n, gn_tempo_out() ? GN_INK : gn_c(sel()));
        if (gn_tempo_out())                       /* outside the genre's range: underlined */
            for (; x1 > x + 5; x1 -= 3)
                cv_rect(x1 - 2, 17, 2, 1, GN_INK);
    }
    if (gxb_ok) {
        const char *pn = gx_part_name((gx_genre_t)gxb_comp.r[0].genre, gxb_comp.part);
        int32_t w = text_w(&FONT_XS, pn) + 8, bx = 236 - 30 - w;
        frame(bx, 2, w, 16, GN_INK);
        cv_text(bx + 4, 2, &FONT_XS, pn, GN_INK);
    } else {
        text_r(206, 3, &FONT_XS, "FX: new idea", GN_MUTED);
    }
    t[0] = 0;
    itoa_u((uint32_t)bar + 1u, n);
    scat(t, n, 24);
    scat(t, "/8", 24);
    text_r(236, 3, &FONT_XS, t, GN_MUTED);
}

static void gn_rows(void)
{
    int r, i;
    for (r = 0; r < 4; r++) {
        int32_t y = 20 + r * 20;
        int on = r == sel(), muted = s4[r].mute;
        uint16_t c = muted ? GN_OFF : on ? gn_c(r) : gn_dim(r);
        uint8_t vel[16], len[16];
        int ps = gn_playing_step(r), bar = (ps >= 0 ? ps : page() * 16) / 16;
        if (on)
            pad(1, y + 1, 238, 18, GN_PANEL);
        if (on)
            cv_rect(4, y + 5, 10, 10, c);
        else
            frame(4, y + 5, 10, 10, c);
        cv_text(19, y + 2, &FONT_S, gn_track_label(r), muted ? GN_MUTED : on ? GN_INK : RGB(185, 185, 192));
        gn_bar(r, bar, vel, len);
        cv_rect(70, y + 16, 128, 1, on ? c : mix(c, GN_BG, 140));
        for (i = 0; i < 16; i++) {
            int32_t x = 70 + i * 8, h;
            if (!vel[i])
                continue;
            h = 3 + vel[i] * 11 / 127;
            cv_rect(x + 2, y + 16 - h, 3, h, c);
        }
        if (muted)
            text_r(236, y + 3, &FONT_XS, "MUTE", GN_MUTED);
        else {
            if (gn_locked(r))
                gn_lock_icon(206, y + 5, GN_INK);
            if (gxb_ok && gxb_comp.r[r].mut_n) {
                char t[4] = {'M', (char)('0' + gxb_comp.r[r].mut_n), 0, 0};
                text_r(236, y + 3, &FONT_XS, t, c);
            }
        }
    }
}

static void gn_macro_pic(int m, int v, int32_t x, int32_t y, uint16_t acc)   /* 52 x 30 */
{
    int i;
    int32_t b = y + 29;
    switch (m & 3) {
    case 0: {                                    /* density: bars lit */
        static const uint8_t H[6] = {16, 24, 12, 28, 20, 14};
        int n = 1 + v * 6 / 128;
        for (i = 0; i < 6; i++) {
            if (i < n)
                cv_rect(x + i * 9, b - H[i], 6, H[i], acc);
            else
                cv_rect(x + i * 9, b - 3, 6, 3, GN_OFF);
        }
        break;
    }
    case 1: {                                    /* syncopation: pulses pushed off the beat */
        int32_t sh = v * 6 / 127, px = x;
        for (i = 0; i < 4; i++) {
            int32_t p0 = x + i * 13 + sh;
            cv_line(px, b, p0, b, GN_INK);
            cv_line(p0, b, p0, y + 6, GN_INK);
            cv_line(p0, y + 6, p0 + 5, y + 6, GN_INK);
            cv_line(p0 + 5, y + 6, p0 + 5, b, GN_INK);
            cv_rect(p0 + 1, y + 7, 4, b - y - 7, acc);
            px = p0 + 5;
        }
        cv_line(px, b, x + 51, b, GN_INK);
        break;
    }
    case 2: {                                    /* space: the tail after a hit */
        int32_t k = 2 + v / 5, py = y + 2;
        for (i = 0; i < 52; i++) {
            int32_t yy = b - 27 * k / (k + i * 2);
            cv_rect(x + i, yy, 1, b - yy, mix(acc, GN_BG, 90));
            if (i)
                cv_line(x + i - 1, py, x + i, yy, GN_INK);
            py = yy;
        }
        break;
    }
    default: {                                   /* groove: the off-beats pushed late */
        int32_t sw = v * 7 / 127, lx = x, ly = b;
        for (i = 0; i < 4; i++) {
            int32_t ux = x + i * 13 + (i & 1 ? sw : 0) + 6, uy = (i & 1) ? y + 10 : y + 4;
            cv_line(lx, ly, ux, uy, GN_INK);
            cv_rect(ux - 1, uy - 1, 3, 3, acc);
            lx = ux;
            ly = b;
            cv_line(ux, uy, ux + 6 > x + 51 ? x + 51 : ux + 6, b, GN_INK);
            lx = ux + 6;
        }
        break;
    }
    }
}

static void gn_macros(void)
{
    static const char *const N[GX_NMACRO] = {"Density", "Sync", "Space", "Groove", "Human", "Tension", "Complex",
                                             "Evolve"};
    int i, hi = shift() ? 4 : 0;
    uint16_t acc = gn_c(sel());
    cv_rect(4, 101, 232, 1, GN_LINE);
    for (i = 0; i < 4; i++) {
        int m = hi + i, v = gn_macro(m);
        int32_t x = 6 + i * 59;
        char t[6];
        cv_text(x, 103, &FONT_XS, N[m], GN_INK);
        gn_macro_pic(m, v, x, 121, acc);
        cv_text(x, 154, &FONT_XS, itoa_u((uint32_t)v, t), (gen.mpend >> m & 1u) ? acc : GN_MUTED);
        cv_text(x + 22, 154, &FONT_XS, i == 0 ? "K1" : i == 1 ? "K2" : i == 2 ? "K3" : "K4", GN_OFF);
    }
}

/* GLO held: what the white keys do now. The six parts as tiles (W9-W14; the part chosen filled, outlined while
   it waits for the next bar), and the mutate keys (W1-W5) */
static void gn_glo_layer(void)
{
    static const char *const K[6] = {"W9", "W10", "W11", "W12", "W13", "W14"};
    int k, waiting = s4p.on && s4p.next[0] != 0xFF;
    uint16_t acc = gn_c(sel());
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

static void gn_strip(void)
{
    int s = sel(), i, ps = gn_playing_step(s), p = page();
    uint8_t vel[16], len[16];
    uint16_t acc = gn_c(s);
    char t[28], n[8];
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
static void gn_top(void)
{
    gn_header();
    gn_rows();
}

static void draw_gen(void)
{
    uint32_t h;
    int r, msg = ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0;
    if (!gen.drawn) {
        gen.sig[0] = gen.sig[1] = gen.sig[2] = 0;
        gen.drawn = 1;
    }
    /* the top: the header and the tracks */
    h = hash(hash(hash(1u, sel()), s4g.tempo), (uint32_t)(gn_playing_step(0) / 16) + 7u * (uint32_t)page());
    h = hash(hash(h, gen.gpend ? gen.genre : 0xFFu), gxb_ok ? (uint32_t)gxb_comp.part << 8 | gxb_comp.lock : 0x1234u);
    h = hash(h, (uint32_t)gn_key() << 8 | (uint32_t)gn_tempo_out());
    for (r = 0; r < 4; r++) {
        int ps = gn_playing_step(r);
        h = hash(hash(h, s4[r].mute), gxb_ok ? gxb_comp.r[r].mut_n | (uint32_t)gxb_comp.r[r].genre << 4 : 0u);
        h = gn_sig_bar(h, r, (ps >= 0 ? ps : page() * 16) / 16);
    }
    if (h != gen.sig[0]) {
        gen.sig[0] = h;
        gn_band(0, 100, gn_top);
    }
    /* the macros */
    h = hash(hash(2u, sel()), (uint32_t)shift() << 8 | gen.mpend);
    for (r = 0; r < (int)GX_NMACRO; r++)
        h = hash(h, (uint32_t)gn_macro(r));
    if (h != gen.sig[1]) {
        gen.sig[1] = h;
        gn_band(100, 72, gn_macros);
    }
    /* the steps */
    h = hash(hash(hash(3u, sel()), page()), (uint32_t)(gn_playing_step(sel()) + 1));
    h = gn_sig_bar(h, sel(), page());
    h = hash(h, msg ? hash_s(hash_s(5u, ui.msg[0]), ui.msg[1]) : 0u);
    h = hash(h, s4[sel()].ch);
    h = hash(h, (uint32_t)shift() << 9 | (uint32_t)(gxb_ok ? gxb_comp.part : 7u) << 1 | (uint32_t)(s4p.on && s4p.next[0] != 0xFF));
    if (gxb_ok && gxb_harm_ok) {                  /* the chords shown: the key, the progression, the bar playing */
        int b;
        h = hash(hash(h, gxb_comp.key), (uint32_t)(gn_playing_step(GX_ROLE_CHORDS) / 16 + 2));
        for (b = 0; b < 8; b++)
            h = hash(h, (uint32_t)gxb_harm.deg[b] << 16 | (uint32_t)gxb_harm.pcs[b]);
    }
    if (h != gen.sig[2]) {
        gen.sig[2] = h;
        gn_band(172, 68, gn_strip);
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

static void gen_key_down(int k)
{
    int w = white_of(k), b = black_of(k);
    if (w >= 0) {
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
        int r = sel();
        ui.btn_used |= 1u << b | 1u << B_GLO;
        if (gn_ensure())
            return 1;
        if (gn_locked(r))
            say(gn_track_label(r), "LOCKED");
        else if (gn_result(gxb_reseed(r, s4_rand()), "RE-ROLL") >= 0)
            say("RE-ROLL", gn_track_label(r));
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
    case B_HOME:                                 /* (SEQ opens the grid) twice: the patterns */
        if (plat_ms() - ui.up_t[B_HOME] < DTAP_MS)
            set_view(V_PATS);
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
        say("SEED", t);
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
            gn_result(gxb_compose(gen.seed), "SEED");
        gn_seed_text(t);
        say("SEED", t);
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
    gen.seed = 1;
}

static int gen_left(void)                        /* the page was on screen: it is not now */
{
    int was = gen.drawn;
    gen.drawn = 0;
    return was;
}
