/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 UI: the screen (part of ui.c). Lit pads on black: the grid is the steps as pads that glow in the
 * sequencer's colour, ties joined and softer, the playhead a lighter column; above it the status bar,
 * below it a line of words and the four knobs. Each band is drawn only when what it shows has changed.
 *
 *   y   0 ..  23  status: the sequencer, its mode and pattern, the tempo, the transport
 *   y  24 .. 171  the grid: 8 rows x 16 pads (drawn in two bands of four rows)
 *   y 172 .. 195  the info line: a message, a menu's name, the step held, the scale and range
 *   y 196 .. 239  the knobs */

#define C_BG RGB(12, 12, 16)
#define C_PAD RGB(34, 36, 44)
#define C_PAD_OUT RGB(21, 22, 27)
#define C_PAD_PH RGB(66, 68, 84)
#define C_T1 RGB(226, 228, 236)
#define C_T2 RGB(146, 148, 160)
#define C_T3 RGB(86, 88, 100)
#define C_RULE RGB(32, 33, 40)
#define C_RECR RGB(255, 60, 60)
static const uint16_t SEQ_RGB[12] = {RGB(0, 190, 255), RGB(255, 70, 190), RGB(255, 170, 0), RGB(80, 230, 120),
                                     RGB(160, 110, 255), RGB(255, 80, 70), RGB(60, 110, 255), RGB(240, 230, 60),
                                     RGB(255, 140, 200), RGB(255, 120, 30), RGB(230, 230, 236), RGB(0, 210, 180)};

static uint16_t mix(uint16_t a, uint16_t b, uint32_t t)   /* a toward b, t/256 */
{
    int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    int32_t k = (int32_t)(t > 256u ? 256u : t);
    ar += (br - ar) * k / 256;
    ag += (bg - ag) * k / 256;
    ab += (bb - ab) * k / 256;
    return (uint16_t)((uint32_t)ar << 11 | (uint32_t)ag << 5 | (uint32_t)ab);
}
static uint16_t seq_c(int s) { return SEQ_RGB[s4[s & 3].color % 12u]; }

static void pad(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* corners cut */
{
    cv_rect(x + 1, y, w - 2, 1, c);
    cv_rect(x, y + 1, w, h - 2, c);
    cv_rect(x + 1, y + h - 1, w - 2, 1, c);
}
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x + 1, y, w - 2, 1, c);
    cv_rect(x + 1, y + h - 1, w - 2, 1, c);
    cv_rect(x, y + 1, 1, h - 2, c);
    cv_rect(x + w - 1, y + 1, 1, h - 2, c);
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(rx - text_w(f, s), y, f, s, c);
}
static void fit(const felucca_font_t *f, char *s, int32_t w)   /* cut to fit */
{
    int n = slen(s);
    while (n > 1 && text_w(f, s) > w)
        s[--n] = 0;
}
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (*s)
        h = hash(h, (uint8_t)*s++);
    return hash(h, 0);
}

/* ------------------------------------------------------------------------- the grid's state --- */
#define GX0 24
#define GW 11                                    /* a pad's width; columns 13 apart, a beat's 2 more */
#define GH 15                                    /* ... its height; rows 18 apart */
#define GY0 24
#define GBAND 74
static int32_t col_x(int c) { return GX0 + c * 13 + (c >> 2) * 2; }
enum {                                           /* a cell: its level (low byte), these */
    CF_ON = 1u << 8, CF_TIE = 1u << 9, CF_SOFT = 1u << 10, CF_MUTE = 1u << 11, CF_COND = 1u << 12, CF_RTG = 1u << 13,
    CF_UP = 1u << 14, CF_DN = 1u << 15, CF_OUT = 1u << 16, CF_PH = 1u << 17, CF_HELD = 1u << 18, CF_CUR = 1u << 19,
    CF_BAR = 1u << 20, CF_DIM = 1u << 21
};
static uint32_t cell[8][16];                     /* [row from the bottom][column] */
static char row_lbl[8][6];
static uint8_t row_hot[8];                       /* a label bright (a root, the track selected) */

static void cell_note(int r, int c, int vel, uint32_t fl)   /* a note at a row, off the grid: at its edge */
{
    if (r > 7) {
        r = 7;
        fl |= CF_UP;
    } else if (r < 0) {
        r = 0;
        fl |= CF_DN;
    }
    if ((cell[r][c] & CF_ON) && !(cell[r][c] & CF_SOFT) && (fl & CF_SOFT))
        return;                                  /* (a chord's root wins over its tones) */
    cell[r][c] = (cell[r][c] & (CF_OUT | CF_PH | CF_HELD | CF_CUR)) | CF_ON | fl | (uint32_t)(vel & 0xFF);
}
static uint32_t step_flags(int cond, int rtg)
{
    uint32_t f = 0;
    if (rtg & S4_MUTED)
        f |= CF_MUTE;
    if (cond != 100)
        f |= CF_COND;
    if (rtg & (S4_RTG_N | S4_SKIP))
        f |= CF_RTG;
    return f;
}
static int range_has(int lo, int hi, int i)
{
    if (lo > hi) {
        int x = lo;
        lo = hi;
        hi = x;
    }
    return i >= lo && i <= hi;
}
static void grid_state(void)
{
    int s = sel(), t = trk(), c, r, slen_ = s4_scale_len(sq()->scale >= S4_HARMO ? 0 : sq()->scale);
    int ph = s4p.on ? s4p.step[s][mode() == M_MULTI || mode() == M_MATRIX ? t : 0] : -1;
    const s4_seq_t *q = sq();
    char nb[8];
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 16; c++)
            cell[r][c] = 0;
        row_lbl[r][0] = 0;
        row_hot[r] = 0;
    }
    for (c = 0; c < 16; c++) {                   /* the columns: in range, the playhead, held, the cursor */
        int i = step_of(c);
        uint32_t f = 0;
        if (mode() == M_MATRIX) {
            const s4_lane_t *ln = &q->d.mx[t & 3].lane[ui.mx_lane];
            if (!range_has(ln->init, ln->end, c))
                f |= CF_OUT;
            if (ph >= 0 && (ph & 15) == c)
                f |= CF_PH;
        } else if (mode() == M_STOCH) {
            if (ui.held >> c & 1u)
                f |= CF_HELD;
        } else if (mode() != M_MULTI) {
            if (!range_has(q->init, q->end, i))
                f |= CF_OUT;
            if (i == ph)
                f |= CF_PH;
        }
        if (mode() != M_STOCH && (ui.held >> c & 1u))
            f |= CF_HELD;
        if (ui.srec && !s4p.on && i == ui.cursor && blink())
            f |= CF_CUR;
        for (r = 0; r < 8; r++)
            cell[r][c] = f;
    }
    if (ui.view == V_MOD) {                      /* a mod lane: its values as bars */
        int l = lane_of_knob();
        for (c = 0; c < 16; c++) {
            int v = q->mod[l][step_of(c)], lv = v * 8 / 128;
            for (r = 0; r <= lv; r++)
                cell[r][c] |= CF_ON | CF_BAR | (uint32_t)(r == lv ? 160 + v * 95 / 127 : 110);
        }
        scopy(row_lbl[7], "127", 6);
        scopy(row_lbl[4], "64", 6);
        scopy(row_lbl[0], "0", 6);
        return;
    }
    switch (mode()) {
    case M_MONO:
    case M_CHORD:
    case M_POLY:
        for (r = 0; r < 8; r++) {
            int d = row_deg(r);
            s4_note_name(s4_note_of(s, d, 0), nb);
            scopy(row_lbl[r], nb, 6);
            row_hot[r] = (uint8_t)((d % slen_ + slen_) % slen_ == 0 ? 1 : 0);
            if (d == ui.cur_deg[s])
                row_hot[r] = 2;
        }
        for (c = 0; c < 16; c++) {
            int i = step_of(c);
            if (mode() == M_MONO) {
                const s4_step_t *st = &q->d.mono[i];
                const s4_step_t *pv = i > 0 ? &q->d.mono[i - 1] : 0;
                uint32_t fl;
                if (!st->vel)
                    continue;
                fl = step_flags(st->cond, st->rtg);
                if (pv && pv->vel && pv->gate == S4_TIE && pv->deg == st->deg && pv->oct == st->oct && c > 0)
                    fl |= CF_TIE;
                cell_note(st->deg + st->oct * slen_ - proj.row[s], c, st->vel * 2, fl);
            } else if (mode() == M_CHORD) {
                const s4_cstep_t *ch = &q->d.chord[i];
                const s4_cstep_t *pv = i > 0 ? &q->d.chord[i - 1] : 0;
                uint8_t n[16];
                int k, cnt, root;
                uint32_t fl;
                if (!ch->vel)
                    continue;
                fl = step_flags(ch->cond, ch->rtg);
                if (pv && pv->vel && pv->gate == S4_TIE && pv->deg == ch->deg && c > 0)
                    fl |= CF_TIE;
                cnt = s4_chord_notes(s, ch, n);
                root = s4_note_of(s, ch->deg, ch->oct);
                for (k = 0; k < cnt; k++)
                    if (n[k] != root)
                        cell_note(s4_deg_of(s, n[k]) - proj.row[s], c, ch->vel, fl | CF_SOFT);
                cell_note(ch->deg + ch->oct * slen_ - proj.row[s], c, ch->vel * 2, fl);
            }
        }
        if (mode() == M_POLY) {
            int k, lo = page() * 16;
            for (k = 0; k < q->d.poly.n; k++) {
                const s4_pnote_t *p = &q->d.poly.note[k];
                uint32_t fl;
                if (p->step < lo || p->step >= lo + 16)
                    continue;
                fl = step_flags(p->cond, p->rtg);
                if (p->step > lo) {
                    int j = s4_poly_find(s, p->step - 1, p->deg);
                    if (j >= 0 && q->d.poly.note[j].gate == S4_TIE)
                        fl |= CF_TIE;
                }
                cell_note(p->deg + p->oct * slen_ - proj.row[s], p->step - lo, p->vel * 2, fl);
            }
        }
        break;
    case M_MULTI:
        for (r = 0; r < 8; r++) {                /* the tracks, the first at the top */
            int tt = 7 - r, tph = s4p.on ? s4p.step[s][tt] : -1;
            const s4_trk_t *k = &q->d.multi.t[tt];
            int muted = k->mute;
            row_lbl[r][0] = (char)('1' + tt);
            row_lbl[r][1] = 0;
            row_hot[r] = (uint8_t)(tt == t ? 2 : 0);
            for (c = 0; c < 16; c++) {
                int i = step_of(c);
                const s4_step_t *st = &q->d.multi.s[tt][i];
                if (!range_has(k->init, k->end, i))
                    cell[r][c] |= CF_OUT;
                if (i == tph)
                    cell[r][c] |= CF_PH;
                if (st->vel) {
                    uint32_t fl = step_flags(st->cond, st->rtg) | (muted ? CF_DIM : 0);
                    const s4_step_t *pv = i > 0 ? &q->d.multi.s[tt][i - 1] : 0;
                    if (pv && pv->vel && pv->gate == S4_TIE && c > 0)
                        fl |= CF_TIE;
                    cell_note(r, c, st->vel * 2, fl);
                } else if (k->dg_on && tt < 3 && s4p.on && i == tph && s4p.lit[s][tt])
                    cell_note(r, c, 120, CF_SOFT);
                if (tt != t && !(cell[r][c] & CF_ON))
                    cell[r][c] &= ~(uint32_t)CF_HELD;
            }
        }
        break;
    case M_STOCH: {
        const s4_stoch_t *st = &q->d.stoch;
        static const char *const NM[4] = {"Oct", "Div", "Ptc", "Rhy"};
        for (c = 0; c < 16; c++) {
            if (c < 12 || c >= 14) {
                int v = c < 12 ? st->prob[c] : c == 14 ? st->pitch : st->rhythm;
                for (r = 0; r < v + (v > 0); r++)
                    cell[r][c] |= CF_ON | CF_BAR | (uint32_t)(r == v ? 230 : 120);
            } else {                             /* the two ways: up from the middle, down from it */
                int up = c == 12 ? st->oct_up : st->div_long, dn = c == 12 ? st->oct_dn : st->div_short;
                for (r = 0; r < up; r++)
                    cell[4 + r][c] |= CF_ON | CF_BAR | 200u;
                for (r = 0; r < dn; r++)
                    cell[3 - r][c] |= CF_ON | CF_BAR | 140u;
            }
        }
        for (r = 0; r < 8; r++) {
            row_lbl[r][0] = (char)('0' + r);
            row_lbl[r][1] = 0;
        }
        (void)NM;
        break;
    }
    case M_MATRIX: {
        const s4_mxtrk_t *x = &q->d.mx[t & 3];
        int l = ui.mx_lane;
        for (c = 0; c < 16; c++) {
            int v = x->v[l][c], lv = -1, best = 9999, k;
            if (l == L_TRIG)
                lv = v ? 7 : -1;
            else
                for (k = 0; k < 8; k++) {
                    int d = MX_LV[l][k] - v;
                    d = d < 0 ? -d : d;
                    if (d < best) {
                        best = d;
                        lv = k;
                    }
                }
            for (r = 0; r <= lv; r++)
                cell[r][c] |= CF_ON | CF_BAR | (uint32_t)(r == lv ? 230 : 120) | (x->p[l][c] && x->p[l][c] < 100 ? CF_COND : 0);
        }
        for (r = 0; r < 8; r++) {
            int v = MX_LV[l][r];
            if (l == L_TRIG)
                break;
            itoa_s(v > 100 ? 100 : v, row_lbl[r]);
            fit(&FONT_XS, row_lbl[r], 21);
        }
        break;
    }
    default:
        break;
    }
}

/* the patterns' view: four rows of sixteen, a sequencer each */
static void pats_state(void)
{
    int s, p, r, c;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 16; c++)
            cell[r][c] = 0;
        row_lbl[r][0] = 0;
        row_hot[r] = 0;
    }
    for (s = 0; s < S4_NSEQ; s++) {
        r = 7 - s * 2;
        row_lbl[r][0] = (char)('1' + s);
        row_lbl[r][1] = 0;
        row_hot[r] = (uint8_t)(s == sel() ? 2 : 0);
        for (p = 0; p < S4_NPAT; p++) {
            uint32_t f = 0;
            if (project_has_pattern(s, p))
                f |= CF_ON | 110u;
            if (p == proj.pat[s])
                f = CF_ON | 255u;
            if (s4p.on && p == s4p.pat[s] && beat_blink())
                f |= CF_PH;
            if (s4p.next[s] == p)
                f = blink() ? (CF_ON | 255u) : (f | CF_CUR);
            if (s4[s].mute && (f & CF_ON))
                f |= CF_DIM;
            cell[r][p] = f;
            cell[r - 1][p] = f;                  /* (two rows tall) */
        }
    }
}
static void song_state(void)
{
    const s4_song_t *g = &s4song[proj.song % S4_NSONG];
    int s, k, r, c;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 16; c++)
            cell[r][c] = 0;
        row_lbl[r][0] = 0;
        row_hot[r] = 0;
    }
    for (s = 0; s < S4_NSEQ; s++) {
        r = 7 - s * 2;
        row_lbl[r][0] = (char)('1' + s);
        row_lbl[r][1] = 0;
        row_hot[r] = (uint8_t)(s == ui.song_lane ? 2 : 0);
        for (k = 0; k < S4_NSLOT; k++) {
            const s4_slot_t *sl = &g->slot[s][k];
            uint32_t f = 0;
            if (sl->pat == S4_BLANK)
                f = CF_ON | CF_SOFT | 60u;
            else if (sl->pat != S4_EMPTY)
                f = CF_ON | (uint32_t)(140 + (sl->rep ? sl->rep : 8) * 6);
            if (s4p.song_on && s4p.slot[s] == k)
                f |= CF_PH;
            if (s == ui.song_lane && k == ui.slot && ui.menu == MN_SLOT)
                f |= CF_HELD;
            cell[r][k] = f | (uint32_t)(sl->pat < S4_BLANK ? (sl->pat + 1u) << 24 : 0u);
            cell[r - 1][k] = f & ~0xFF000000u;
        }
    }
}

/* ------------------------------------------------------------------------ the grid's pixels --- */
static void draw_cell(int r, int c, int32_t y, uint16_t col, int32_t gh)
{
    uint32_t f = cell[r][c];
    int32_t x = col_x(c);
    uint16_t base = (f & CF_OUT) ? C_PAD_OUT : (f & CF_PH) ? C_PAD_PH : C_PAD;
    if (f & CF_ON) {
        uint32_t lv = f & 0xFFu;
        uint16_t lit, top;
        if (lv > 255u)
            lv = 255u;
        if (f & CF_SOFT)
            lv = lv / 3u + 30u;
        if (f & CF_DIM)
            lv = lv / 3u;
        lit = mix(mix(base, col, 90u + lv * 166u / 255u), C_T1, (f & CF_PH) ? 120u : 0u);
        if (f & CF_OUT)
            lit = mix(lit, C_BG, 140u);
        top = mix(lit, C_T1, 70u);
        if (!(f & (CF_SOFT | CF_BAR | CF_DIM)))     /* the glow: a halo into the gaps */
            frame(x - 1, y - 1, GW + 2, gh + 2, mix(C_BG, col, 70u));
        if (f & CF_TIE)                           /* tied: joined to the one before it */
            cv_rect(x - (c & 3 ? 2 : 4), y + 3, c & 3 ? 2 : 4, gh - 6, mix(lit, C_BG, 80u));
        if (f & CF_MUTE) {
            frame(x, y, GW, gh, lit);
            cv_rect(x + 2, y + 2, GW - 4, gh - 4, base);
        } else {
            pad(x, y, GW, gh, lit);
            if (!(f & (CF_SOFT | CF_BAR)))
                cv_rect(x + 1, y + 1, GW - 2, 1, top);
        }
        if (f & CF_COND)                          /* a chance or a condition: a corner cut out */
            cv_rect(x + GW - 3, y + 1, 2, 2, C_BG);
        if (f & CF_RTG) {                         /* repeats: two ticks */
            cv_rect(x + 2, y + gh - 3, 2, 1, C_BG);
            cv_rect(x + GW - 4, y + gh - 3, 2, 1, C_BG);
        }
        if (f & CF_UP)
            cv_rect(x + 3, y, GW - 6, 2, C_T1);
        if (f & CF_DN)
            cv_rect(x + 3, y + gh - 2, GW - 6, 2, C_T1);
        if (f >> 24) {                           /* a pattern's number (the songs) */
            char t[4];
            itoa_u(f >> 24, t);
            text_c(x + GW / 2 + 1, y + (gh - 12) / 2, &FONT_XS, t, C_BG);
        }
    } else
        pad(x, y, GW, gh, base);
    if (f & CF_HELD)
        frame(x - 1, y - 1, GW + 2, gh + 2, C_T1);
    if (f & CF_CUR)
        frame(x - 1, y - 1, GW + 2, gh + 2, C_RECR);
}
static uint32_t grid_sig(uint16_t col)
{
    uint32_t h = 2166136261u;
    int r, c;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 16; c++)
            h = hash(h, cell[r][c]);
        h = hash_s(hash(h, row_hot[r]), row_lbl[r]);
    }
    return hash(hash(h, col), ui.view);
}
static void draw_grid(void)
{
    uint16_t col = ui.view == V_PATS || ui.view == V_SONG ? C_T2 : seq_c(sel());
    int band, r, c;
    uint32_t h;
    if (ui.view == V_PATS)
        pats_state();
    else if (ui.view == V_SONG)
        song_state();
    else
        grid_state();
    h = grid_sig(col);
    if (h == ui.sig[1] && !ui.help)
        return;
    ui.sig[1] = h;
    for (band = 0; band < 2; band++) {
        cv_begin(240, GBAND, C_BG);
        for (r = 0; r < 4; r++) {
            int row = 7 - (band * 4 + r);        /* (rows from the bottom; the top is drawn first) */
            int32_t y = r * 18 + 2;
            uint16_t lc = row_hot[row] == 2 ? (ui.view == V_GRID || ui.view == V_KEYS || ui.view == V_MOD ? col : C_T1)
                                            : row_hot[row] ? C_T2 : C_T3;
            if (ui.view == V_PATS || ui.view == V_SONG) {
                int s = (7 - row) / 2;
                lc = row_hot[row] == 2 ? C_T1 : C_T3;
                if (row_lbl[row][0]) {           /* the sequencer's number, lit if it is the one picked */
                    pad(3, y, 17, GH + 18, row_hot[row] == 2 ? seq_c(s) : mix(C_BG, seq_c(s), 110u));
                    text_c(12, y + 10, &FONT_XS, row_lbl[row], C_BG);
                }
                for (c = 0; c < 16; c++)           /* (a pattern a pad two rows tall) */
                    if ((ui.view == V_PATS || c < S4_NSLOT) && (row & 1))
                        draw_cell(row, c, y, seq_c(s), 15 + 18);
                (void)lc;
                continue;
            }
            if (row_lbl[row][0])
                text_r(GX0 - 4, y + 1, &FONT_XS, row_lbl[row], lc);
            for (c = 0; c < 16; c++)
                draw_cell(row, c, y, col, 15);
        }
        cv_blit(0, GY0 + band * GBAND);
    }
}

/* ------------------------------------------------------------------------------- help cards --- */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT -", "OCT +"};
static const char *const HELP[NB][5] = {
    {"Random perform: velocity,", "octave, triggers, repeats", "GLO+FX: the generators", "(random, Euclid, drums)",
     "FX on one: it writes"},
    {"Division and direction", "Twice: the loop", "Held + steps: the length", "(two steps: start, end)",
     "Held + < >: half, twice"},
    {"The mod lanes (CCs)", "Black keys: levels to draw", "OCT, SELECT: the lane", "GLO+ENV: what it sends", ""},
    {"The two LFOs", "(again: the second)", "GLO+LFO: MIDI mappings", "", ""},
    {"Undo (GLO+EDIT: redo)", "Held + a step: copy, paste", "Held + >: duplicate", "Held + OCT: transpose",
     "Held + REC: clear"},
    {"", "", "", "", ""},
    {"Back to the grid", "Held: the patterns", "GLO+HOME: the songs", "HOME+SAVE: a snapshot",
     "HOME+PLAY: to the top"},
    {"Save this pattern", "Held + a pattern key:", "save into it", "", ""},
    {"The arpeggiator", "Held + a chord step:", "its chord on the knobs", "GLO+ARP: hold", ""},
    {"The keyboard", "GLO+SEQ: the sequencer", "(again: track or lane)", "", ""},
    {"", "", "", "", ""},
    {"Record (stopped: by step)", "Twice: capture what", "you just played", "GLO+REC: its settings",
     "REC+PLAY: count in"},
    {"The rows down", "GLO: an octave", "Keyboard: its octave", "A step held: its octave", ""},
    {"The rows up", "GLO: an octave", "Keyboard: its octave", "A step held: its octave", ""},
};
static void draw_help(void)
{
    int b = ui.help - 1, i;
    uint32_t h = hash(0x5EC4u, (uint32_t)ui.help);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    {
        int band;
        for (band = 0; band < 2; band++) {       /* (two bands: the canvas holds 100 rows at most) */
            cv_begin(240, GBAND, C_BG);
            cv_oy = -band * GBAND;             /* (the card's rows band * 74 on, into this canvas) */
            cv_y0 = 0;
            cv_y1 = GBAND;
            pad(10, 8, 220, 132, RGB(26, 27, 34));
            frame(10, 8, 220, 132, seq_c(sel()));
            cv_text(22, 16, &FONT_B, BTN_NAME[b], seq_c(sel()));
            for (i = 0; i < 5; i++)
                cv_text(22, 42 + i * 18, &FONT_S, HELP[b][i], C_T1);
            cv_blit(0, GY0 + band * GBAND);
        }
    }
    ui.sig[1] = h;
}

/* ------------------------------------------------------------------------------ the status bar --- */
static void draw_status(void)
{
    int s = sel();
    char t[16], u[8];
    uint32_t h;
    int playing = s4p.on, counting = s4p.count != 0, rec = s4p.rec || ui.srec, nx = s4p.next[s];
    int tempo = s4p.ext ? s4p.tempo : s4g.tempo;
    h = hash(hash(hash(hash(hash(1u, (uint32_t)s), (uint32_t)mode()), proj.pat[s]), (uint32_t)nx),
             (uint32_t)tempo);
    h = hash(hash(hash(hash(h, (uint32_t)playing), (uint32_t)counting << 8 | s4p.count), (uint32_t)rec),
             (uint32_t)(nx != 0xFF ? blink() : 0));
    h = hash(hash(hash(h, seq_c(s)), s4p.ext), (uint32_t)(playing ? beat_blink() : 0));
    h = hash(hash(h, s4p.snap), s4p.song_on);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, 24, C_BG);
    pad(4, 3, 26, 18, seq_c(s));
    t[0] = (char)('1' + s);
    t[1] = 0;
    text_c(17, 4, &FONT_B, t, C_BG);
    scopy(t, MODE_N[mode()], 16);
    cv_text(36, 4, &FONT_B, t, C_T1);
    t[0] = 'P';
    itoa_u((uint32_t)proj.pat[s] + 1u, t + 1);
    {
        int32_t x = cv_text(108, 4, &FONT_B, t, seq_c(s));
        if (nx != 0xFF && nx != proj.pat[s] && blink()) {
            u[0] = '>';
            itoa_u((uint32_t)nx + 1u, u + 1);
            cv_text(x + 4, 4, &FONT_B, u, C_T2);
        }
    }
    itoa_u((uint32_t)tempo / 10u, t);
    scat(t, ".", 16);
    scat(t, itoa_u((uint32_t)tempo % 10u, u), 16);
    text_r(196, 4, &FONT_B, t, s4p.ext ? RGB(120, 200, 255) : C_T1);
    if (playing && beat_blink())
        cv_rect(152, 19, 44, 2, seq_c(s));
    if (counting) {                              /* the count-in: beats left */
        itoa_u(s4p.count, t);
        text_c(222, 4, &FONT_B, t, C_RECR);
    } else if (playing) {                        /* a triangle */
        int i;
        for (i = 0; i < 7; i++)
            cv_rect(214 + i, 5 + i, 1, 14 - 2 * i, rec ? C_RECR : C_T1);
    } else {
        cv_rect(214, 6, 12, 12, rec ? C_RECR : C_T3);
    }
    if (rec && !counting)
        cv_rect(230, 9, 6, 6, C_RECR);
    if (s4p.snap)
        cv_rect(4, 22, 26, 2, C_T1);
    if (s4p.song_on)
        cv_rect(36, 22, 60, 2, C_T2);
    cv_rect(0, 23, 240, 1, C_RULE);
    cv_blit(0, 0);
}

/* ------------------------------------------------------------------- the info line, the knobs --- */
typedef struct {
    char lbl[14], val[14];
    int16_t frac;                                /* 0..1000 the value's place, -1 none, < -1 centred (bipolar) */
    uint8_t hot;
} kcell_t;
static kcell_t kc[4];
static char info_l[32], info_r[24];
static uint16_t info_c;

static void kset(int i, const char *lbl, const char *val, int frac)
{
    scopy(kc[i].lbl, lbl, 14);
    scopy(kc[i].val, val, 14);
    kc[i].frac = (int16_t)frac;
}
static int frac_of(int v, int lo, int hi) { return hi > lo ? (v - lo) * 1000 / (hi - lo) : 0; }
static void span_text(char *b, int lo, int hi, int fmt)
{
    char u[12];
    fmt_val(fmt, lo, b);
    if (hi != lo) {
        scat(b, "-", 14);
        fmt_val(fmt, hi, u);
        scat(b, u, 14);
    }
}
static int step_first(void)
{
    int k;
    for (k = 0; k < 16; k++)
        if (ui.held >> k & 1u)
            return step_of(k);
    return -1;
}
static void knob_cells(void)
{
    int s = sel(), i, t = trk(), touched = ui.touched && plat_ms() - ui.touch_t < 1500u ? ui.touched - 1 : -1;
    char b[16];
    for (i = 0; i < 4; i++) {
        kset(i, "", "", -1);
        kc[i].hot = (uint8_t)(i == touched);
    }
    if (ui.menu) {
        for (i = 0; i < 4; i++) {
            prm_t x;
            if (!menu_prm(ui.menu, ui.page, i, &x))
                continue;
            fmt_val(x.fmt, prm_get(&x), b);
            kset(i, x.label, b, x.hi - x.lo > 1 ? frac_of(prm_get(&x), x.lo, x.hi) : -1);
            if (x.lo < 0 && x.hi > 0)
                kc[i].frac = (int16_t)(-2 - frac_of(prm_get(&x), x.lo, x.hi));
        }
        return;
    }
    if (shift() && !ui.held) {
        fmt_val(F_BPM, s4g.tempo, b);
        kset(0, "Tempo", b, frac_of(s4g.tempo, 300, 3000));
        fmt_val(F_PCT, sq()->swing ? sq()->swing : 50, b);
        kset(1, "Swing", b, frac_of(sq()->swing ? sq()->swing : 50, 50, 75));
        kset(2, "Scale", s4_scale_name(sq()->scale), frac_of(sq()->scale, 0, S4_HARMO));
        kset(3, "Root", ROOT_N[sq()->root % 12u], frac_of(sq()->root, 0, 11));
        return;
    }
    if (ui.view == V_KEYS) {
        itoa_u(proj.kvel ? proj.kvel : 100, b);
        kset(0, "Velocity", b, frac_of(proj.kvel ? proj.kvel : 100, 1, 127));
        itoa_s(ui.bend / 64, b);
        kset(1, "Bend", b, -2 - frac_of(ui.bend, -8192, 8191));
        itoa_u(ui.modw, b);
        kset(2, "Mod wheel", b, frac_of(ui.modw, 0, 127));
        kset(3, "Roller", ui.roll_i ? s4_div_name(ROLL_DV[ui.roll_i]) : "Off", frac_of(ui.roll_i, 0, 7));
        return;
    }
    if (ui.view == V_MOD) {
        int l = lane_of_knob(), i0 = step_first();
        const s4_modl_t *m = &sq()->ml[l];
        if (i0 >= 0) {
            itoa_u(sq()->mod[l][i0], b);
            kset(0, "Value", b, frac_of(sq()->mod[l][i0], 0, 127));
        } else {
            span_text(b, ui.span_lo, ui.span_hi, F_NUM);
            kset(0, "All", touched == 0 ? b : "", -1);
        }
        itoa_u((uint32_t)l + 1u, b);
        kset(1, mode() == M_MULTI ? "Track" : "Lane", b, -1);
        if (m->xtype == X_CC) {
            scopy(b, "CC ", 16);
            itoa_u(m->xnum, b + 3);
        } else
            scopy(b, XT_N[m->xtype % X_N], 16);
        kset(2, "Sends", b, -1);
        itoa_u(m->smooth, b);
        kset(3, "Smooth", b, frac_of(m->smooth, 0, 30));
        return;
    }
    if (ui.view == V_PATS || ui.view == V_SONG) {
        itoa_u((uint32_t)proj.song + 1u, b);
        kset(0, "Song", b, -1);
        fmt_val(F_BPM, s4g.tempo, b);
        kset(1, "Tempo", b, -1);
        fmt_val(F_QUANT, s4g.loadq, b);
        kset(2, "Patterns at", b, -1);
        itoa_u(plat_log_capacity() ? plat_log_live() * 100u / plat_log_capacity() : 0u, b);
        scat(b, "%", 16);
        kset(3, "Flash used", b, -1);
        return;
    }
    if (ui.held) {                               /* the first step held: its values */
        int i0 = step_first(), sh = shift(), arp = btn_held(B_ARP);
        switch (mode()) {
        case M_MONO:
        case M_MULTI: {
            const s4_step_t *x = nstep(s, t, i0);
            if (!x->vel)
                break;
            if (!sh) {
                itoa_u(x->vel, b);
                kset(0, "Velocity", b, frac_of(x->vel, 1, 127));
                if (mode() == M_MULTI) {
                    s4_note_name(sq()->d.multi.t[t].note + x->deg, b);
                    kset(1, "Note", b, -1);
                } else {
                    itoa_s(x->oct, b);
                    kset(1, "Octave", b, -2 - frac_of(x->oct, -4, 4));
                }
                fmt_val(F_GATE, x->gate, b);
                kset(2, "Gate", b, frac_of(x->gate, 1, S4_LEG));
                itoa_u(sq()->mod[lane_of_knob()][i0], b);
                kset(3, "Mod", b, frac_of(sq()->mod[lane_of_knob()][i0], 0, 127));
            } else {
                fmt_val(F_PCT, x->glide, b);
                kset(0, "Glide", b, frac_of(x->glide, 0, 100));
                fmt_val(F_COND, x->cond, b);
                kset(1, "Condition", b, -1);
                fmt_val(F_RTG, x->rtg, b);
                kset(2, "Repeats", b, -1);
                itoa_s(x->ofs, b);
                scat(b, "%", 16);
                kset(3, "Offset", b, -2 - frac_of(x->ofs, -45, 45));
            }
            return;
        }
        case M_CHORD: {
            const s4_cstep_t *x = &sq()->d.chord[i0];
            if (!x->vel)
                break;
            if (arp) {
                char cn[24];
                s4_chord_name(s, x, cn);
                itoa_u((uint32_t)x->type + 1u, b);
                kset(0, "Type", b, -1);
                kset(1, "Voicing", VOI_N[x->voi % 6u], -1);
                itoa_u(x->spread, b);
                kset(2, "Spread", b, frac_of(x->spread, 0, 7));
                fit(&FONT_B, cn, 56);
                kset(3, "Chord", cn, -1);
            } else if (!sh) {
                itoa_u(x->vel, b);
                kset(0, "Velocity", b, frac_of(x->vel, 1, 127));
                itoa_s(x->oct, b);
                kset(1, "Octave", b, -2 - frac_of(x->oct, -4, 4));
                fmt_val(F_GATE, x->gate, b);
                kset(2, "Gate", b, frac_of(x->gate, 1, S4_LEG));
                itoa_u(sq()->mod[ui.lane & 3][i0], b);
                kset(3, "Mod", b, frac_of(sq()->mod[ui.lane & 3][i0], 0, 127));
            } else {
                itoa_s(x->strum, b);
                kset(0, "Strum", b, -2 - frac_of(x->strum, -100, 100));
                fmt_val(F_COND, x->cond, b);
                kset(1, "Condition", b, -1);
                fmt_val(F_RTG, x->rtg, b);
                kset(2, "Repeats", b, -1);
                itoa_s(x->ofs, b);
                scat(b, "%", 16);
                kset(3, "Offset", b, -2 - frac_of(x->ofs, -45, 45));
            }
            return;
        }
        case M_POLY: {
            int k;
            for (k = 0; k < sq()->d.poly.n; k++)
                if (sq()->d.poly.note[k].step == i0) {
                    const s4_pnote_t *x = &sq()->d.poly.note[k];
                    if (!sh) {
                        itoa_u(x->vel, b);
                        kset(0, "Velocity", b, frac_of(x->vel, 1, 127));
                        itoa_s(x->oct, b);
                        kset(1, "Octave", b, -2 - frac_of(x->oct, -4, 4));
                        fmt_val(F_GATE, x->gate, b);
                        kset(2, "Gate", b, frac_of(x->gate, 1, S4_LEG));
                        itoa_u(sq()->mod[ui.lane & 3][i0], b);
                        kset(3, "Mod", b, frac_of(sq()->mod[ui.lane & 3][i0], 0, 127));
                    } else {
                        fmt_val(F_PCT, sq()->d.poly.glide[i0], b);
                        kset(0, "Glide", b, frac_of(sq()->d.poly.glide[i0], 0, 100));
                        fmt_val(F_COND, x->cond, b);
                        kset(1, "Condition", b, -1);
                        fmt_val(F_RTG, x->rtg, b);
                        kset(2, "Repeats", b, -1);
                        itoa_s(x->ofs, b);
                        scat(b, "%", 16);
                        kset(3, "Offset", b, -2 - frac_of(x->ofs, -45, 45));
                    }
                    return;
                }
            break;
        }
        case M_MATRIX: {
            const s4_mxtrk_t *x = &sq()->d.mx[t & 3];
            int j = i0 & 15, l = ui.mx_lane;
            if (l == L_GATE)
                fmt_val(F_GATE, x->v[l][j], b);
            else
                itoa_s(x->v[l][j], b);
            kset(0, LANE_N[l], b, -1);
            if (l == L_PULS) {
                static const char *const PT[3] = {"All", "First", "Tied"};
                kset(1, "Pulses play", PT[x->p[l][j] % 3u], -1);
            } else {
                fmt_val(F_PCT, x->p[l][j] ? x->p[l][j] : 100, b);
                kset(1, "Chance", b, frac_of(x->p[l][j] ? x->p[l][j] : 100, 0, 100));
            }
            return;
        }
        default:
            break;
        }
    }
    /* nothing held: the knobs move every step (their spread shows as they turn) */
    switch (mode()) {
    case M_STOCH:
    case M_MATRIX:
        itoa_u(sq()->vdef ? sq()->vdef : 100, b);
        kset(0, "Velocity", b, frac_of(sq()->vdef, 1, 127));
        if (mode() == M_MATRIX) {
            itoa_s(sq()->d.mx[t & 3].note, b);
            kset(1, "Note", b, -2 - frac_of(sq()->d.mx[t & 3].note, -24, 24));
        } else {
            itoa_u(sq()->oct, b);
            kset(1, "Octave", b, frac_of(sq()->oct, 0, 7));
        }
        fmt_val(F_PCT, sq()->gdef ? sq()->gdef : 75, b);
        kset(2, "Gate", b, frac_of(sq()->gdef, 1, 100));
        break;
    default:
        if (touched == 0)
            span_text(b, ui.span_lo, ui.span_hi, F_NUM);
        else
            itoa_u(sq()->vdef ? sq()->vdef : 100, b);
        kset(0, touched == 0 ? "Velocity all" : "Velocity", b, -1);
        if (mode() == M_MULTI)
            s4_note_name(sq()->d.multi.t[t].note, b);
        else {
            scopy(b, ROOT_N[sq()->root % 12u], 16);
            itoa_u(sq()->oct, b + slen(b));
        }
        kset(1, mode() == M_MULTI ? "Note" : "Base", b, -1);
        if (touched == 2)
            span_text(b, ui.span_lo, ui.span_hi, F_PCT);
        else
            fmt_val(F_PCT, sq()->gdef ? sq()->gdef : 75, b);
        kset(2, touched == 2 ? "Gate all" : "Gate", b, -1);
        break;
    }
    {
        int l = lane_of_knob();
        const s4_modl_t *m = &sq()->ml[l];
        if (touched == 3)
            span_text(b, ui.span_lo, ui.span_hi, F_NUM);
        else if (m->xtype == X_CC) {
            scopy(b, "CC ", 16);
            itoa_u(m->xnum, b + 3);
        } else
            scopy(b, XT_N[m->xtype % X_N], 16);
        kset(3, touched == 3 ? "Mod all" : "Mod", b, -1);
    }
}
static void info_text(void)
{
    int s = sel(), t = trk();
    char b[16];
    info_l[0] = info_r[0] = 0;
    info_c = C_T2;
    if (ui.msg_until && (int32_t)(plat_ms() - ui.msg_until) < 0) {
        scopy(info_l, ui.msg[0], 32);
        scopy(info_r, ui.msg[1], 24);
        info_c = C_T1;
        return;
    }
    if (ui.menu) {
        scopy(info_l, MENU_NAME[ui.menu], 32);
        if (ui.menu == MN_SETUP)
            scopy(info_r, SETUP_PAGE[ui.page % 8u], 24);
        else if (ui.menu == MN_SEQ)
            scopy(info_r, SEQ_PAGE[ui.page % 5u], 24);
        else if (ui.menu == MN_LFO)
            scopy(info_r, ui.page < 3 ? "LFO 1" : "LFO 2", 24);
        else if (ui.menu == MN_MAP) {
            scopy(info_r, "Map ", 24);
            scat(info_r, itoa_u((uint32_t)ui.map_i + 1u, b), 24);
        } else if (ui.menu == MN_TRACK || ui.menu == MN_EUCLID || ui.menu == MN_DRUMS) {
            scopy(info_r, "Track ", 24);
            scat(info_r, itoa_u((uint32_t)t + 1u, b), 24);
        } else if (ui.menu == MN_LANE)
            scopy(info_r, LANE_N[ui.mx_lane], 24);
        else if (ui.menu == MN_MODSET) {
            scopy(info_r, "Lane ", 24);
            scat(info_r, itoa_u((uint32_t)ui.lane + 1u, b), 24);
        } else if (ui.menu == MN_SLOT) {
            scopy(info_r, "Slot ", 24);
            scat(info_r, itoa_u((uint32_t)ui.slot + 1u, b), 24);
        }
        info_c = seq_c(s);
        return;
    }
    if (ui.view == V_PATS) {
        scopy(info_l, btn_held(B_SAVE) ? "Save into a pattern" : "Patterns", 32);
        scopy(info_r, "Seq ", 24);
        scat(info_r, itoa_u((uint32_t)s + 1u, b), 24);
        return;
    }
    if (ui.view == V_SONG) {
        scopy(info_l, "Song ", 32);
        scat(info_l, itoa_u((uint32_t)proj.song + 1u, b), 32);
        scopy(info_r, s4p.song_on ? "Playing" : "Lane ", 24);
        if (!s4p.song_on)
            scat(info_r, itoa_u((uint32_t)ui.song_lane + 1u, b), 24);
        return;
    }
    if (ui.srec && !s4p.on) {
        scopy(info_l, "Step record: ", 32);
        scat(info_l, itoa_u((uint32_t)ui.cursor + 1u, b), 32);
        scopy(info_r, "SEL tie  EDIT rest", 24);
        info_c = C_RECR;
        return;
    }
    if (ui.view == V_KEYS) {
        scopy(info_l, mode() == M_CHORD ? "Chord keys" : mode() == M_MULTI ? "Drum pads" : "Keyboard", 32);
        if (mode() == M_CHORD) {
            scopy(info_r, "Type ", 24);
            scat(info_r, itoa_u((uint32_t)ui.kb_type + 1u, b), 24);
            scat(info_r, "  ", 24);
            scat(info_r, VOI_N[ui.kb_voi % 6], 24);
        } else {
            scopy(info_r, "Octave ", 24);
            scat(info_r, itoa_s(proj.koct[s], b), 24);
        }
        return;
    }
    if (ui.view == V_MOD) {
        const s4_modl_t *m = &sq()->ml[lane_of_knob()];
        scopy(info_l, mode() == M_MULTI ? "Mod: track " : "Mod lane ", 32);
        scat(info_l, itoa_u((uint32_t)lane_of_knob() + 1u, b), 32);
        scopy(info_r, m->on ? "On" : "Off", 24);
        return;
    }
    if (mode() == M_STOCH) {
        static const char *const COL[16] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
                                            "Octaves up/down", "Long/short", "New pitch", "New rhythm"};
        scopy(info_l, "Chances: ", 32);
        scat(info_l, COL[ui.stoch_col & 15], 32);
        scopy(info_r, s4_div_name(sq()->div), 24);
        return;
    }
    if (mode() == M_MATRIX) {
        scopy(info_l, "Track ", 32);
        scat(info_l, itoa_u((uint32_t)(t & 3) + 1u, b), 32);
        scat(info_l, ": ", 32);
        scat(info_l, LANE_N[ui.mx_lane], 32);
        scopy(info_r, s4_div_name(sq()->d.mx[t & 3].lane[ui.mx_lane].div ? sq()->d.mx[t & 3].lane[ui.mx_lane].div : DV_16), 24);
        return;
    }
    if (mode() == M_MULTI) {
        const s4_trk_t *k = &sq()->d.multi.t[t];
        scopy(info_l, "Track ", 32);
        scat(info_l, itoa_u((uint32_t)t + 1u, b), 32);
        scat(info_l, "  Ch ", 32);
        scat(info_l, itoa_u((uint32_t)k->ch + 1u, b), 32);
        b[0] = (char)('A' + (k->port & 3));
        b[1] = 0;
        scat(info_l, b, 32);
        scopy(info_r, s4_div_name(k->div), 24);
        return;
    }
    scopy(info_l, ROOT_N[sq()->root % 12u], 32);
    scat(info_l, " ", 32);
    scat(info_l, s4_scale_name(sq()->scale), 32);
    scopy(info_r, s4_div_name(sq()->div), 24);
}
static void draw_bottom(void)
{
    int i, s = sel(), pages = last_page();
    uint32_t h = 77u;
    uint16_t col = seq_c(s);
    knob_cells();
    info_text();
    for (i = 0; i < 4; i++)
        h = hash(hash(hash_s(hash_s(h, kc[i].lbl), kc[i].val), (uint32_t)kc[i].frac), kc[i].hot);
    h = hash(hash(hash_s(hash_s(h, info_l), info_r), info_c), col);
    h = hash(hash(hash(h, (uint32_t)page()), (uint32_t)pages), ui.menu ? (uint32_t)ui.page << 8 | menu_pages(ui.menu) : 0u);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, 68, C_BG);
    cv_rect(0, 0, 240, 1, C_RULE);
    {                                            /* the info line, and the pages (or the menu's pages) as dots */
        int n = ui.menu ? menu_pages(ui.menu) : (ui.view == V_GRID || ui.view == V_MOD) ? S4_NPAGE : 0, cur = ui.menu ? ui.page : page();
        int32_t dx = 236 - n * 7, rx;
        if (ui.view == V_GRID && (mode() == M_STOCH || mode() == M_MATRIX) && !ui.menu)
            n = 0, dx = 236;
        for (i = 0; i < n; i++) {
            uint16_t c = i == cur ? (ui.menu ? col : C_T1) : (!ui.menu && i > pages) ? C_RULE : C_T3;
            cv_rect(dx + i * 7 + 1, 9, 5, 5, c);
        }
        rx = n ? dx - 6 : 236;
        {
            char l[32], r[24];
            scopy(l, info_l, 32);
            scopy(r, info_r, 24);
            fit(&FONT_S, r, 90);
            text_r(rx, 4, &FONT_S, r, C_T2);
            fit(&FONT_B, l, rx - text_w(&FONT_S, r) - 14);
            cv_text(6, 4, &FONT_B, l, info_c);
        }
    }
    cv_rect(0, 23, 240, 1, C_RULE);
    for (i = 0; i < 4; i++) {
        int32_t x = i * 60, cx = x + 30;
        kcell_t *k = &kc[i];
        uint16_t vc = k->hot ? col : C_T1;
        if (i)
            cv_rect(x, 28, 1, 36, C_RULE);
        if (!k->lbl[0])
            continue;
        {
            char l[14];
            scopy(l, k->lbl, 14);
            fit(&FONT_XS, l, 56);
            text_c(cx, 27, &FONT_XS, l, k->hot ? col : C_T3);
        }
        {
            char v[14];
            scopy(v, k->val, 14);
            if (text_w(&FONT_B, v) > 56) {
                fit(&FONT_XS, v, 56);
                text_c(cx, 44, &FONT_XS, v, vc);
            } else
                text_c(cx, 42, &FONT_B, v, vc);
        }
        if (k->frac >= 0) {                      /* the value's place */
            int32_t w = 44 * k->frac / 1000;
            cv_rect(cx - 22, 62, 44, 2, C_RULE);
            cv_rect(cx - 22, 62, w, 2, k->hot ? col : C_T2);
        } else if (k->frac < -1) {               /* bipolar: from the middle */
            int32_t f = -2 - k->frac, p = 44 * f / 1000 - 22;
            cv_rect(cx - 22, 62, 44, 2, C_RULE);
            if (p >= 0)
                cv_rect(cx, 62, p ? p : 1, 2, k->hot ? col : C_T2);
            else
                cv_rect(cx + p, 62, -p, 2, k->hot ? col : C_T2);
        }
    }
    cv_blit(0, 172);
}

/* ------------------------------------------------------------------------------ the frame --- */
void ui_init(void)
{
    int s;
    memset(&ui, 0, sizeof ui);
    ui.row = -1;
    ui.first_abs = -1;
    ui.view = V_GRID;
    for (s = 0; s < S4_NSEQ; s++)
        ui.cur_deg[s] = (int8_t)proj.row[s];
    s4_select(sel());
    s4_rec_track(trk());
    ui.cap_rd = s4_cap_total();
    lcd_fill(0, 0, 240, 240, C_BG);
}

void (*ui_mark)(int part);                       /* a development build's clock: where a frame's time goes */
#define UI_MARK_(p)                                                                                          \
    do {                                                                                                     \
        if (ui_mark)                                                                                         \
            ui_mark(p);                                                                                      \
    } while (0)

void ui_frame(void)
{
    UI_MARK_(0);
    input();
    service();
    UI_MARK_(1);
    draw_status();
    UI_MARK_(2);
    if (ui.help)
        draw_help();
    else
        draw_grid();
    UI_MARK_(3);
    draw_bottom();
    UI_MARK_(4);
    leds();
    UI_MARK_(5);
    ui.frame++;
}

void ui_input_only(void)
{
    input();
    service();
}

#ifdef SEQ4_BENCH
void ui_dev_view(int v, int s)                   /* SysEx 0C: a view and a sequencer, as if picked */
{
    close_menu();
    set_sel(s & 3);
    set_view(v % 5);
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
}
#endif
