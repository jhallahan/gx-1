/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. The drawing is ui_draw.c, the button cards ui_help.c.
 *
 *   ALGORITHM   the sequencer (1-4)                 PRESETS   its pattern (in at the next boundary)
 *   white keys  the page's 16 steps                 black keys 1-8 the rows (notes, or Multitrack's
 *               (tap: on/off; hold one and press      tracks); 9 and 10 the page back and on; 11 MUTE
 *               another: a tie; hold: its values    KNOB 1-4  velocity, octave, gate, mod (a step held:
 *               on the knobs)                         its own; GLO held: tempo, swing, scale, root)
 *   PLAY        play / stop (held: fill)            REC       record (stopped: step by step)
 *   HOME        back to the grid (held: patterns)   SAVE      save (held: patterns to save into)
 *   SEQ         the keyboard                        ARP       the arpeggiator
 *   FX          random                              LFO       the LFOs
 *   ENV         the mod lanes                       SEL       division, range, loop
 *   EDIT        undo (held: copy, paste)            GLO       shift (held long: setup)
 *   OCT- OCT+   the rows (the keyboard: its octave)  SELECT   the menus' pages; the grid's rows */
#include <stddef.h>
#include "../seq/fastmath.h"

enum { V_GRID, V_KEYS, V_MOD, V_PATS, V_SONG, V_GEN };
enum { MN_NONE, MN_DIV, MN_LOOP, MN_RANDP, MN_RANDG, MN_EUCLID, MN_DRUMS, MN_LFO, MN_ARP, MN_SEQ, MN_TRACK,
       MN_MODSET, MN_MAP, MN_REC, MN_SETUP, MN_NUDGE, MN_LANE, MN_SLOT, MN_MASTER, MN_N };

#define HOLD_MS 260u                   /* a key held this long is held (not tapped) */
#define HELP_MS 900u                   /* a button held alone this long shows its card */
#define DTAP_MS 320u                   /* two taps this close are a double tap */
#define SETUP_MS 700u                  /* GLO held alone this long: setup */

typedef struct {                       /* the clipboard */
    uint8_t kind, s, t, n, mode, first;
    s4_step_t st[16];
    s4_cstep_t cs[16];
    uint8_t mod[16];
    uint8_t pat;
    s4_slot_t slot;
} clip_t;
enum { CK_NONE, CK_STEPS, CK_TRACK, CK_SEQ, CK_PAGE, CK_PAT, CK_SLOT };

static struct {
    uint8_t view, menu, page;
    uint32_t btn, keys, btn_used, keys_used;
    uint32_t down_t[NB], up_t[NB];
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t turn_t;
    uint8_t help;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t frame;
    uint32_t sig[8];
    uint8_t shift_latch, mute_latch;
    uint16_t held, held_used;          /* white keys held (the page's steps); those that did more than a tap */
    int16_t first_abs;                 /* the first step held (absolute: a tie can cross pages), -1 */
    int8_t row;                        /* a black row key held, -1 none */
    uint8_t row_used;
    uint8_t key_note[NKEYS];           /* the keyboard: the note each key plays (+1), 0 none */
    uint8_t key_seq[NKEYS], key_trk[NKEYS];
    uint8_t hand[8], nhand;            /* pick and place: the notes played last */
    s4_cstep_t hand_chord;             /* ... the chord played last */
    uint8_t hand_chord_ok;
    int8_t cur_deg[S4_NSEQ];           /* the row a tapped step gets */
    int8_t kb_type, kb_voi, kb_spread, kb_bass;
    uint8_t roll;
    int16_t bend;
    uint32_t bend_t;
    uint8_t arm;                       /* Multitrack keyboard: the track armed (+1) */
    uint8_t srec, cursor, srec_any;    /* step recording (REC, stopped): its cursor */
    clip_t clip;
    uint8_t edit_taken;
    uint8_t lane;                      /* the mod lane on screen */
    uint8_t mx_lane;
    uint8_t map_i;
    uint8_t song_lane;
    uint8_t pats_save;
    uint8_t pats_latched;
    uint32_t act_t;
    uint8_t touched;                   /* the knob last turned (+1) */
    uint32_t touch_t;
    int16_t span_lo, span_hi;          /* a global knob's spread (min, max) after its turn */
    uint8_t stoch_col;
    uint8_t lfo_i;
    uint32_t learn_in;                 /* MIDI learn: the monitor's count when it began (+1) */
    uint8_t loop_set;                  /* loop menu: the first of two steps pressed (+1) */
    uint8_t snap;
    uint8_t menu_undo;                 /* the menu's turns have their undo */
    uint32_t knob_t;                   /* the last knob turned on the grid (a new gesture: an undo) */
    uint8_t pending_pat;               /* PRESETS turned: the pattern to load (+1), at pending_t */
    uint32_t pending_t;
    uint32_t cap_rd;                   /* the notes played: read so far (s4_cap_get) */
    uint32_t hand_t;                   /* ... the last one's time (a chord: those close together) */
    uint8_t made;                      /* white keys whose press made their step (let go: kept) */
    uint16_t made_mask;
    uint8_t sel_first;                 /* SEL held + steps: the first (+1) */
    uint8_t slot;                      /* the song slot picked */
    uint8_t kc_notes[8], kc_n, kc_key; /* the chord keyboard: the chord sounding, its key (+1) */
    uint8_t modw;                      /* the keyboard's mod wheel */
    uint8_t roll_i;                    /* ... its roller (an index into ROLL_DV) */
    uint8_t knob_k;                    /* the knob last turned on the grid */
    uint8_t pats_prev, pats_mom;       /* HOME / SAVE held: the patterns shown for as long (the view before) */
    uint8_t rec_tap;                   /* REC's tap did this (undone by a double tap) */
    uint8_t fill;
    uint8_t clear_arm;                 /* GLO + EDIT held long on the patterns: armed */
} ui;
static float lfo_hz_ui(int r) { return 0.05f * fm_exp2f((float)(r - 10) * (11.97f / 49.0f)); }   /* (s4_mod.c's) */

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static void scopy(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void scat(char *d, const char *s, int n)
{
    int i = slen(d);
    scopy(d + i, s, n - i);
}
static void say(const char *a, const char *b)
{
    scopy(ui.msg[0], a ? a : "", 24);
    scopy(ui.msg[1], b ? b : "", 24);
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
static char *itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    char *r = b;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return r;
}
static char *itoa_s(int v, char *b)              /* with its sign when not 0 */
{
    char *r = b;
    if (v > 0)
        *b++ = '+';
    else if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    itoa_u((uint32_t)v, b);
    return r;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* the white and black keys among the 27 */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int white_of(int k)
{
    int i;
    for (i = 0; i < 16; i++)
        if (WHITE_KEY[i] == k)
            return i;
    return -1;
}
static int black_of(int k)
{
    int i;
    for (i = 0; i < 11; i++)
        if (BLACK_KEY[i] == k)
            return i;
    return -1;
}
#define BK_PREV 8
#define BK_NEXT 9
#define BK_MUTE 10

static int sel(void) { return proj.sel & 3; }
static s4_seq_t *sq(void) { return &s4[sel()]; }
static int mode(void) { return sq()->mode; }
static int trk(void) { return proj.trk[sel()] & 7; }
static int page(void) { return proj.page[sel()] & 7; }
static int step_of(int w) { return page() * 16 + w; }
static int shift(void) { return (ui.btn >> B_GLO & 1u) || ui.shift_latch; }
static int muting(void) { return (ui.keys >> BLACK_KEY[BK_MUTE] & 1u) || ui.mute_latch; }
static int btn_held(int b) { return (int)(ui.btn >> b & 1u); }
static void dirty(void) { project_dirty_seq(sel()); }
static void dirty_g(void) { project_dirty_proj(); }
int ui_dirty(void) { return project_is_dirty(); }
static void undo(void) { s4_undo_push(sel()); }

/* knob acceleration: fast turns go further on big ranges */
static uint8_t accel_off;                        /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t e, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(e < 0 ? -e : e), m;
    ui.enc_t[role] = now;
    if (!a)
        return e;
    if (a > 1)
        dt /= a;
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return e;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return e * (int32_t)m;
}

/* ------------------------------------------------------------------------------- the steps --- */
static s4_step_t *nstep(int s, int t, int i)     /* a note step: Mono's, a Multitrack track's, Stochastic's */
{
    if (s4[s].mode == M_MULTI)
        return &s4[s].d.multi.s[t & 7][i & 127];
    if (s4[s].mode == M_STOCH)
        return &s4[s].d.stoch.buf[i & 127];
    return &s4[s].d.mono[i & 127];
}
static int row_deg(int r) { return proj.row[sel()] + r; }   /* a row's degree (Mono, Poly, Chord) */

static void step_init(s4_step_t *st, int deg)
{
    st->deg = (int8_t)deg;
    st->vel = (uint8_t)(sq()->vdef ? sq()->vdef : 100);
    st->gate = (uint8_t)(sq()->gdef ? sq()->gdef : 75);
    st->oct = 0;
    st->cond = 100;
    st->rtg = 0;
    st->ofs = 0;
    st->glide = 0;
}
static void cstep_init(s4_cstep_t *c, int deg)
{
    if (ui.hand_chord_ok && deg == 0x7F) {
        *c = ui.hand_chord;
    } else {
        c->deg = (int8_t)(deg == 0x7F ? ui.cur_deg[sel()] : deg);
        c->type = 0;
        c->voi = 0;
        c->spread = 0;
        c->strum = 0;
        c->oct = 0;
    }
    c->vel = (uint8_t)(sq()->vdef ? sq()->vdef : 100);
    c->gate = (uint8_t)(sq()->gdef ? sq()->gdef : 90);
    c->cond = 100;
    c->rtg = 0;
    c->ofs = 0;
    c->glide = 0;
}
static void pnote_init(s4_pnote_t *p, int step, int deg)
{
    p->step = (uint8_t)step;
    p->deg = (int8_t)deg;
    p->vel = (uint8_t)(sq()->vdef ? sq()->vdef : 100);
    p->gate = (uint8_t)(sq()->gdef ? sq()->gdef : 75);
    p->oct = 0;
    p->cond = 100;
    p->rtg = 0;
    p->ofs = 0;
}
static int step_on(int s, int t, int i)          /* does a step hold anything */
{
    int k;
    switch (s4[s].mode) {
    case M_POLY:
        for (k = 0; k < s4[s].d.poly.n; k++)
            if (s4[s].d.poly.note[k].step == i)
                return 1;
        return 0;
    case M_CHORD: return s4[s].d.chord[i & 127].vel != 0;
    case M_MATRIX: return s4[s].d.mx[t & 3].v[L_TRIG][i & 15] != 0;
    default: return nstep(s, t, i)->vel != 0;
    }
}
static void poly_clear_step(int s, int i)
{
    int k;
    for (k = s4[s].d.poly.n - 1; k >= 0; k--)
        if (s4[s].d.poly.note[k].step == i)
            s4_poly_del(s, k);
}

/* a step switched on or off by a tap (the hand, the row picked, or the chord played last) */
static void tap_step(int i)
{
    int s = sel(), t = trk();
    undo();
    switch (mode()) {
    case M_MONO:
    case M_MULTI: {
        s4_step_t *st = nstep(s, t, i);
        if (st->vel) {
            int k = i, deg = st->deg;
            while (nstep(s, t, k)->gate == S4_TIE && k < 127 && nstep(s, t, k + 1)->deg == deg) {
                nstep(s, t, k)->vel = 0;       /* a tied note goes whole */
                k++;
            }
            nstep(s, t, k)->vel = 0;
        } else
            step_init(st, mode() == M_MULTI ? 0 : ui.cur_deg[s]);
        break;
    }
    case M_POLY:
        if (step_on(s, 0, i))
            poly_clear_step(s, i);
        else {
            int k, n = ui.nhand ? ui.nhand : 1;
            for (k = 0; k < n; k++) {
                s4_pnote_t p;
                pnote_init(&p, i, ui.nhand ? s4_deg_of(s, ui.hand[k]) : ui.cur_deg[s]);
                if (s4_poly_find(s, i, p.deg) < 0)
                    s4_poly_add(s, &p);
            }
        }
        break;
    case M_CHORD: {
        s4_cstep_t *c = &s4[s].d.chord[i];
        if (c->vel)
            c->vel = 0;
        else
            cstep_init(c, 0x7F);
        break;
    }
    case M_MATRIX: {
        s4_mxtrk_t *x = &s4[s].d.mx[t & 3];
        int l = ui.mx_lane, j = i & 15;
        if (l == L_TRIG)
            x->v[l][j] = (int8_t)!x->v[l][j];
        else if (l == L_NOTE || l == L_INTV || l == L_OCT)
            x->v[l][j] = 0;
        break;
    }
    default:
        break;
    }
    dirty();
}

/* Matriceal: a lane's values the rows 1-8 stand for */
static const int8_t MX_LV[S4_NLANE][8] = {
    {0, 1, 1, 1, 1, 1, 1, 1}, {-7, -5, -3, -1, 1, 3, 5, 7}, {-3, -2, -1, 0, 1, 2, 3, 4}, {1, 2, 3, 4, 5, 6, 7, 8},
    {16, 32, 48, 64, 80, 96, 112, 127}, {-3, -2, -1, 0, 1, 2, 3, 3}, {12, 25, 37, 50, 62, 75, 100, S4_TIE},
    {12, 25, 37, 50, 62, 75, 87, 100}, {-1, 1, 1, 2, 2, 3, 4, 5}, {0, 10, 20, 35, 50, 65, 80, 100}};
static const char *const LANE_N[S4_NLANE] = {"Triggers", "Note", "Interval", "Pulses", "Velocity", "Octave",
                                             "Gate", "Chance", "Repeats", "Glide"};

/* a step put on a row (a note there), or taken off it if that note is there */
static void row_step(int r, int i)
{
    int s = sel(), deg = row_deg(r);
    undo();
    switch (mode()) {
    case M_MONO: {
        s4_step_t *st = nstep(s, 0, i);
        if (st->vel && st->deg == deg && !st->oct)
            st->vel = 0;
        else if (st->vel) {
            st->deg = (int8_t)deg;
            st->oct = 0;
        } else
            step_init(st, deg);
        ui.cur_deg[s] = (int8_t)deg;
        break;
    }
    case M_MULTI: {
        s4_step_t *st = nstep(s, r, i);
        if (st->vel)
            st->vel = 0;
        else
            step_init(st, 0);
        break;
    }
    case M_POLY: {
        int k = s4_poly_find(s, i, deg);
        if (k >= 0)
            s4_poly_del(s, k);
        else {
            s4_pnote_t p;
            pnote_init(&p, i, deg);
            s4_poly_add(s, &p);
        }
        ui.cur_deg[s] = (int8_t)deg;
        break;
    }
    case M_CHORD: {
        s4_cstep_t *c = &s4[s].d.chord[i];
        if (c->vel && c->deg == deg)
            c->vel = 0;
        else if (c->vel)
            c->deg = (int8_t)deg;
        else
            cstep_init(c, deg);
        ui.cur_deg[s] = (int8_t)deg;
        break;
    }
    case M_STOCH: {                            /* a column's value: rows 1-8 */
        s4_stoch_t *st = &s4[s].d.stoch;
        int col = i & 15, v = r;
        if (col < 12)
            st->prob[col] = (uint8_t)v;
        else if (col == 12) {
            if (v >= 4)
                st->oct_up = (uint8_t)(v - 4);
            else
                st->oct_dn = (uint8_t)(3 - v);
        } else if (col == 13) {
            if (v >= 4)
                st->div_long = (uint8_t)(v - 4);
            else
                st->div_short = (uint8_t)(3 - v);
        } else if (col == 14)
            st->pitch = (uint8_t)v;
        else
            st->rhythm = (uint8_t)v;
        ui.stoch_col = (uint8_t)col;
        break;
    }
    case M_MATRIX: {                           /* a lane's step as a level: rows 1-8 */
        s4_mxtrk_t *x = &s4[s].d.mx[trk() & 3];
        int j = i & 15;
        x->v[ui.mx_lane][j] = MX_LV[ui.mx_lane][r & 7];
        break;
    }
    default:
        break;
    }
    dirty();
}

/* hold one step, press another: a tie (left to right) or straight notes (right to left) */
static void tie_steps(int a, int b)
{
    int s = sel(), t = trk(), lo = a < b ? a : b, hi = a < b ? b : a, i, straight = b < a;
    undo();
    switch (mode()) {
    case M_MONO:
    case M_MULTI: {
        s4_step_t src = *nstep(s, t, a);
        if (!src.vel)
            step_init(&src, mode() == M_MULTI ? 0 : ui.cur_deg[s]);
        for (i = lo; i <= hi; i++) {
            s4_step_t *x = nstep(s, t, i);
            *x = src;
            x->ofs = 0;
            x->rtg = 0;
            x->gate = straight ? (uint8_t)(sq()->gdef ? sq()->gdef : 75) : i < hi ? S4_TIE : (uint8_t)(sq()->gdef ? sq()->gdef : 75);
        }
        break;
    }
    case M_CHORD: {
        s4_cstep_t src = s4[s].d.chord[a];
        if (!src.vel)
            cstep_init(&src, 0x7F);
        for (i = lo; i <= hi; i++) {
            s4[s].d.chord[i] = src;
            s4[s].d.chord[i].ofs = 0;
            s4[s].d.chord[i].gate = straight ? (uint8_t)(sq()->gdef ? sq()->gdef : 90) : i < hi ? S4_TIE : 96;
        }
        break;
    }
    case M_POLY: {
        int k, n = s4[s].d.poly.n;
        for (k = 0; k < n; k++) {
            s4_pnote_t p = s4[s].d.poly.note[k];
            if (p.step != a)
                continue;
            for (i = lo; i <= hi; i++) {
                int at = s4_poly_find(s, i, p.deg);
                s4_pnote_t q = p;
                q.step = (uint8_t)i;
                q.gate = straight ? (uint8_t)(sq()->gdef ? sq()->gdef : 75) : i < hi ? S4_TIE : (uint8_t)(sq()->gdef ? sq()->gdef : 75);
                if (at >= 0)
                    s4[s].d.poly.note[at] = q;
                else
                    s4_poly_add(s, &q);
            }
        }
        break;
    }
    default:
        break;
    }
    dirty();
    say(straight ? "STRAIGHT" : "TIED", 0);
}

/* ------------------------------------------------------------------- the knobs on the grid --- */
static void lane_live(int l, int v)              /* a mod lane's knob: its MIDI value now */
{
    s4_modl_t *m = &sq()->ml[l & 7];
    int t = mode() == M_MULTI ? (l & 7) : 0;
    if (!m->xtype)
        return;
    switch (m->xtype) {
    case X_CC: s4_live_cc(sel(), t, 0xB0, m->xnum, (uint32_t)v); break;
    case X_PB: s4_live_cc(sel(), t, 0xE0, 0, (uint32_t)v); break;
    case X_AT: s4_live_cc(sel(), t, 0xD0, (uint32_t)v, 0); break;
    case X_PC: s4_live_cc(sel(), t, 0xC0, (uint32_t)v, 0); break;
    default: break;
    }
}
static int lane_of_knob(void) { return mode() == M_MULTI ? trk() : ui.lane & 3; }

/* a global knob: every step's value moved (the ends clip, so turning far evens them out) */
static void global_knob(int k, int e)
{
    int s = sel(), t = trk(), i, lo = 999, hi = -999, any = 0;
    if (k == 1) {                                /* octave: the sequence's base (Multitrack: the track's note) */
        if (mode() == M_MULTI) {
            s4_trk_t *x = &sq()->d.multi.t[t];
            x->note = (uint8_t)clampi(x->note + e, 0, 127);
            ui.span_lo = ui.span_hi = x->note;
        } else if (mode() == M_MATRIX) {
            s4_mxtrk_t *x = &sq()->d.mx[t & 3];
            x->note = (int8_t)clampi(x->note + e, -24, 24);
            ui.span_lo = ui.span_hi = x->note;
        } else {
            sq()->oct = (uint8_t)clampi(sq()->oct + e, 0, 7);
            ui.span_lo = ui.span_hi = sq()->oct;
        }
        dirty();
        return;
    }
    if (k == 3) {                                /* mod: the lane's values (and its MIDI now) */
        int l = lane_of_knob();
        int lim = mode() == M_MULTI ? s4_seq_len(s, t) : s4_seq_len(s, 0), lo2, hi2;
        (void)lim;
        {
            int r0, r1;
            if (mode() == M_MULTI) {
                r0 = sq()->d.multi.t[t].init;
                r1 = sq()->d.multi.t[t].end;
            } else {
                r0 = sq()->init;
                r1 = sq()->end;
            }
            lo2 = r0 < r1 ? r0 : r1;
            hi2 = r0 < r1 ? r1 : r0;
        }
        for (i = lo2; i <= hi2; i++) {
            int v = clampi(sq()->mod[l][i] + e, 0, 127);
            sq()->mod[l][i] = (uint8_t)v;
            if (v < lo)
                lo = v;
            if (v > hi)
                hi = v;
        }
        sq()->ml[l].on = 1;
        lane_live(l, sq()->mod[l][s4p.step[s][mode() == M_MULTI ? t : 0] & 127]);
        ui.span_lo = (int16_t)lo;
        ui.span_hi = (int16_t)hi;
        dirty();
        return;
    }
    switch (mode()) {                            /* velocity (0) or gate (2), every step */
    case M_MONO:
    case M_MULTI:
        for (i = 0; i < S4_NSTEP; i++) {
            s4_step_t *x = nstep(s, t, i);
            if (!x->vel)
                continue;
            if (k == 0)
                x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
            else if (x->gate <= 100)
                x->gate = (uint8_t)clampi(x->gate + e, 1, 100);
            {
                int v = k == 0 ? x->vel : x->gate;
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
            any = 1;
        }
        break;
    case M_CHORD:
        for (i = 0; i < S4_NSTEP; i++) {
            s4_cstep_t *x = &sq()->d.chord[i];
            if (!x->vel)
                continue;
            if (k == 0)
                x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
            else if (x->gate <= 100)
                x->gate = (uint8_t)clampi(x->gate + e, 1, 100);
            {
                int v = k == 0 ? x->vel : x->gate;
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
            any = 1;
        }
        break;
    case M_POLY:
        for (i = 0; i < sq()->d.poly.n; i++) {
            s4_pnote_t *x = &sq()->d.poly.note[i];
            if (k == 0)
                x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
            else if (x->gate <= 100)
                x->gate = (uint8_t)clampi(x->gate + e, 1, 100);
            {
                int v = k == 0 ? x->vel : x->gate;
                if (v < lo)
                    lo = v;
                if (v > hi)
                    hi = v;
            }
            any = 1;
        }
        break;
    case M_STOCH:
        if (k == 0)
            sq()->vdef = (uint8_t)clampi(sq()->vdef + e, 1, 127);
        else
            sq()->gdef = (uint8_t)clampi(sq()->gdef + e, 1, 100);
        lo = hi = k == 0 ? sq()->vdef : sq()->gdef;
        any = 1;
        break;
    default:
        break;
    }
    if (!any) {                                  /* nothing there: the defaults new steps get */
        if (k == 0)
            sq()->vdef = (uint8_t)clampi(sq()->vdef + e, 1, 127);
        else
            sq()->gdef = (uint8_t)clampi(sq()->gdef + e, 1, 100);
        lo = hi = k == 0 ? sq()->vdef : sq()->gdef;
    }
    ui.span_lo = (int16_t)lo;
    ui.span_hi = (int16_t)hi;
    dirty();
}

/* a knob with steps held: their own values */
static int held_steps(int *out)                  /* the absolute steps held, in order */
{
    int n = 0, w;
    for (w = 0; w < 16; w++)
        if (ui.held >> w & 1u)
            out[n++] = step_of(w);
    return n;
}
static int gate_step(int g, int e)               /* gate: 1..100, then tied, then legato */
{
    return clampi(g + e, 1, S4_LEG);
}
static int cond_step(int c, int e) { return clampi(c + e, 0, S4_C_MAX); }
static int rtg_step(int r, int e)                /* x1..x5, then (turned down past x1) skip */
{
    int n = (r & S4_SKIP) ? -1 : (int)(r & S4_RTG_N), m = (int)(r & S4_MUTED);
    n = clampi(n + e, -1, 4);
    return n < 0 ? ((int)S4_SKIP | m) : (n | m);
}
static void step_knob(int k, int e)
{
    int steps[16], n = held_steps(steps), j, s = sel(), t = trk(), sh = shift();
    int arp = btn_held(B_ARP) && mode() == M_CHORD;
    if (!n)
        return;
    for (j = 0; j < n; j++) {
        int i = steps[j];
        switch (mode()) {
        case M_MONO:
        case M_MULTI: {
            s4_step_t *x = nstep(s, t, i);
            if (!x->vel)
                step_init(x, mode() == M_MULTI ? 0 : ui.cur_deg[s]);
            if (!sh) {
                if (k == 0)
                    x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
                else if (k == 1) {
                    if (mode() == M_MULTI)
                        x->deg = (int8_t)clampi(x->deg + e, -48, 48);
                    else
                        x->oct = (int8_t)clampi(x->oct + e, -4, 4);
                } else if (k == 2)
                    x->gate = (uint8_t)gate_step(x->gate, e);
                else {
                    int l = lane_of_knob();
                    sq()->mod[l][i] = (uint8_t)clampi(sq()->mod[l][i] + e, 0, 127);
                    sq()->ml[l].on = 1;
                }
            } else {
                if (k == 0)
                    x->glide = (uint8_t)clampi(x->glide + e, 0, 100);
                else if (k == 1)
                    x->cond = (uint8_t)cond_step(x->cond, e);
                else if (k == 2)
                    x->rtg = (uint8_t)rtg_step(x->rtg, e);
                else
                    x->ofs = (int8_t)clampi(x->ofs + e, -45, 45);
            }
            break;
        }
        case M_CHORD: {
            s4_cstep_t *x = &sq()->d.chord[i];
            if (!x->vel)
                cstep_init(x, 0x7F);
            if (arp) {
                if (k == 0)
                    x->type = (uint8_t)clampi(x->type + e, 0, s4_chord_types(s, x->deg) - 1);
                else if (k == 1)
                    x->voi = (uint8_t)clampi(x->voi + e, 0, 5);
                else if (k == 2)
                    x->spread = (uint8_t)clampi(x->spread + e, 0, 7);
                else
                    x->deg = (int8_t)clampi(x->deg + e, -24, 36);
            } else if (!sh) {
                if (k == 0)
                    x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
                else if (k == 1)
                    x->oct = (int8_t)clampi(x->oct + e, -4, 4);
                else if (k == 2)
                    x->gate = (uint8_t)gate_step(x->gate, e);
                else {
                    sq()->mod[ui.lane & 3][i] = (uint8_t)clampi(sq()->mod[ui.lane & 3][i] + e, 0, 127);
                    sq()->ml[ui.lane & 3].on = 1;
                }
            } else {
                if (k == 0)
                    x->strum = (int8_t)clampi(x->strum + e, -100, 100);
                else if (k == 1)
                    x->cond = (uint8_t)cond_step(x->cond, e);
                else if (k == 2)
                    x->rtg = (uint8_t)rtg_step(x->rtg, e);
                else
                    x->ofs = (int8_t)clampi(x->ofs + e, -45, 45);
            }
            break;
        }
        case M_POLY: {
            int q;
            for (q = 0; q < sq()->d.poly.n; q++) {
                s4_pnote_t *x = &sq()->d.poly.note[q];
                if (x->step != i)
                    continue;
                if (!sh) {
                    if (k == 0)
                        x->vel = (uint8_t)clampi(x->vel + e, 1, 127);
                    else if (k == 1)
                        x->oct = (int8_t)clampi(x->oct + e, -4, 4);
                    else if (k == 2)
                        x->gate = (uint8_t)gate_step(x->gate, e);
                } else {
                    if (k == 1)
                        x->cond = (uint8_t)cond_step(x->cond, e);
                    else if (k == 2)
                        x->rtg = (uint8_t)rtg_step(x->rtg, e);
                    else if (k == 3)
                        x->ofs = (int8_t)clampi(x->ofs + e, -45, 45);
                }
            }
            if (!sh && k == 3) {
                sq()->mod[ui.lane & 3][i] = (uint8_t)clampi(sq()->mod[ui.lane & 3][i] + e, 0, 127);
                sq()->ml[ui.lane & 3].on = 1;
            }
            if (sh && k == 0)
                sq()->d.poly.glide[i] = (uint8_t)clampi(sq()->d.poly.glide[i] + e, 0, 100);
            break;
        }
        case M_MATRIX: {                     /* a lane's step: K1 its value, K2 its chance */
            s4_mxtrk_t *x = &sq()->d.mx[t & 3];
            int l = ui.mx_lane, jj = i & 15;
            static const int8_t LO[S4_NLANE] = {0, -24, -12, 1, 1, -4, 5, 0, -1, 0};
            static const int8_t HI[S4_NLANE] = {1, 24, 12, 8, 127, 4, S4_LEG, 100, 5, 100};
            if (k == 0)
                x->v[l][jj] = (int8_t)clampi(x->v[l][jj] + e, LO[l], HI[l]);
            else if (k == 1)
                x->p[l][jj] = (uint8_t)clampi(x->p[l][jj] + e, 0, l == L_PULS ? 2 : 100);
            break;
        }
        default:
            break;
        }
    }
    ui.held_used |= ui.held;
    dirty();
}

/* ------------------------------------------------------------------------------------ menus --- */
enum { F_NUM, F_SIGNED, F_PCT, F_ONOFF, F_CH, F_PORT, F_NOTE, F_DIV, F_DIR, F_SCALE, F_ROOT, F_BPM, F_DEST,
       F_PARA, F_XTYPE, F_WAVE, F_LRATE, F_ARP, F_MODE, F_COND, F_RTG, F_GATE, F_OFFNUM, F_QUANT, F_MAPTYPE,
       F_MAPCH, F_COLOR, F_CLOCK, F_PORTS, F_THRU, F_UTHRU, F_PLAY, F_MUTEM, F_RECM, F_COUNT, F_MET, F_MS,
       F_SPCT, F_VOI, F_BANK, F_LOOPR, F_LANE, F_SEQ, F_PAT, F_REPS, F_PCSLOT, F_LAUNCH, F_TRKS, F_LFOON,
       F_DENS, F_TPOSE, F_LEARN, F_HARMJ, F_LEDS, F_ROLL, F_NONE };
typedef struct {
    const char *label;
    uint8_t size;                                /* 1 u8, 2 s8 (as int8), 3 u16; 0 none */
    void *p;
    int lo, hi;
    uint8_t fmt;
    uint8_t g;                                   /* 1: a project setting (else the sequencer's) */
} prm_t;
static void P(prm_t *x, const char *label, int size, void *p, int lo, int hi, int fmt)
{
    x->label = label;
    x->size = (uint8_t)size;
    x->p = p;
    x->lo = lo;
    x->hi = hi;
    x->fmt = (uint8_t)fmt;
    x->g = 0;
}
static void PG(prm_t *x, const char *label, int size, void *p, int lo, int hi, int fmt)
{
    P(x, label, size, p, lo, hi, fmt);
    x->g = 1;
}
static int prm_get(const prm_t *x)
{
    switch (x->size) {
    case 1: return *(uint8_t *)x->p;
    case 2: return *(int8_t *)x->p;
    case 3: return *(uint16_t *)x->p;
    default: return 0;
    }
}
static void prm_set(const prm_t *x, int v)
{
    v = clampi(v, x->lo, x->hi);
    switch (x->size) {
    case 1: *(uint8_t *)x->p = (uint8_t)v; break;
    case 2: *(int8_t *)x->p = (int8_t)v; break;
    case 3: *(uint16_t *)x->p = (uint16_t)v; break;
    default: break;
    }
}

static int menu_pages(int m)
{
    switch (m) {
    case MN_DIV: return 2;
    case MN_RANDG: return 2;
    case MN_LFO: return 6;
    case MN_ARP: return 3;
    case MN_SEQ: return mode() == M_CHORD ? 5 : 4;
    case MN_TRACK: return 3;
    case MN_MODSET: return 4;
    case MN_MAP: return 6;
    case MN_REC: return 2;
    case MN_SETUP: return 8;
    case MN_NUDGE: return 2;
    case MN_LANE: return 3;
    default: return 1;
    }
}
static const char *const MENU_NAME[MN_N] = {"", "Division", "Loop", "Random perform", "Random generator",
                                            "Euclid", "Drum generator", "LFO", "Arpeggiator", "Sequencer",
                                            "Track", "Mod lane", "MIDI mapping", "Recording", "Setup",
                                            "Nudge", "Lane", "Song slot", "Song"};
static const char *const SETUP_PAGE[8] = {"Clock", "MIDI in", "MIDI", "Playing", "Arp, loops", "Harmony",
                                          "New steps", "Panel"};
static const char *const SEQ_PAGE[5] = {"MIDI", "Launch", "Sound", "Time", "Chords"};

/* a menu's page: its four values */
static int menu_prm(int m, int pg, int i, prm_t *x)
{
    s4_seq_t *q = sq();
    int t = trk();
    x->p = 0;
    x->size = 0;
    x->label = "";
    x->g = 0;
    switch (m) {
    case MN_DIV:
        if (pg == 0) {
            if (q->mode == M_MULTI) {
                s4_trk_t *k = &q->d.multi.t[t];
                static uint8_t tsel;
                tsel = (uint8_t)t;
                switch (i) {
                case 0: P(x, "Track", 1, &tsel, 0, 7, F_LANE); x->g = 2; break;
                case 1: P(x, "Direction", 1, &k->dir, 0, D_N - 1, F_DIR); break;
                case 2: P(x, "Random div", 1, &k->rdiv, 0, 100, F_PCT); break;
                default: P(x, "Division", 1, &k->div, 0, DV_N - 1, F_DIV); break;
                }
            } else {
                switch (i) {
                case 0: P(x, "Rows", 2, &proj.row[sel()], -48, 60, F_SIGNED); x->g = 1; break;
                case 1: P(x, "Direction", 1, &q->dir, 0, D_N - 1, F_DIR); break;
                case 2: P(x, "Random div", 1, &q->rdiv, 0, 100, F_PCT); break;
                default: P(x, "Division", 1, &q->div, 0, DV_N - 1, F_DIV); break;
                }
            }
        } else {
            uint8_t *in = q->mode == M_MULTI ? &q->d.multi.t[t].init : &q->init;
            uint8_t *en = q->mode == M_MULTI ? &q->d.multi.t[t].end : &q->end;
            uint8_t *lo = q->mode == M_MULTI ? &q->d.multi.t[t].loop_on : &q->loop_on;
            switch (i) {
            case 0: P(x, "Start", 1, in, 0, 127, F_NUM); break;
            case 1: P(x, "End", 1, en, 0, 127, F_NUM); break;
            case 2: P(x, "Loop", 1, lo, 0, 1, F_ONOFF); break;
            default: P(x, "Loop rate", 1, &q->loop_rate, 0, DV_N, F_LOOPR); break;
            }
        }
        break;
    case MN_LOOP: {
        uint8_t *la = q->mode == M_MULTI ? &q->d.multi.t[t].loop_a : &q->loop_a;
        uint8_t *lb = q->mode == M_MULTI ? &q->d.multi.t[t].loop_b : &q->loop_b;
        uint8_t *lo = q->mode == M_MULTI ? &q->d.multi.t[t].loop_on : &q->loop_on;
        switch (i) {
        case 0: P(x, "From", 1, la, 0, 127, F_NUM); break;
        case 1: P(x, "To", 1, lb, 0, 127, F_NUM); break;
        case 2: P(x, "Rate", 1, &q->loop_rate, 0, DV_N, F_LOOPR); break;
        default: P(x, "Loop", 1, lo, 0, 1, F_ONOFF); break;
        }
        break;
    }
    case MN_RANDP:
        if (q->mode == M_MULTI) {
            s4_trk_t *k = &q->d.multi.t[t];
            switch (i) {
            case 0: P(x, "Velocity", 2, &k->rp_vel, -100, 100, F_SPCT); break;
            case 1: P(x, "Gate", 2, &k->rp_gate, -100, 100, F_SPCT); break;
            case 2: P(x, "Triggers", 1, &k->rp_trig, 0, 100, F_PCT); break;
            default: P(x, "Retrig", 2, &k->rp_rtg, -100, 100, F_SPCT); break;
            }
        } else {
            switch (i) {
            case 0: P(x, "Velocity", 2, &q->rp_vel, -100, 100, F_SPCT); break;
            case 1: P(x, "Octave", 2, &q->rp_oct, -100, 100, F_SPCT); break;
            case 2: P(x, "Triggers", 1, &q->rp_trig, 0, 100, F_PCT); break;
            default: P(x, "Retrig", 2, &q->rp_rtg, -100, 100, F_SPCT); break;
            }
        }
        break;
    case MN_RANDG:
        if (pg == 0)
            switch (i) {
            case 0: P(x, "Bias", 2, &q->rg_bias, -24, 24, F_SIGNED); break;
            case 1: P(x, "Range", 2, &q->rg_range, -24, 24, F_SIGNED); break;
            case 2: P(x, "Variety", 1, &q->rg_rand, 0, 100, F_PCT); break;
            default: P(x, "Density", 1, &q->rg_dens, 1, 104, F_DENS); break;
            }
        else
            switch (i) {
            case 0: P(x, "Scale too", 1, &q->rg_scale, 0, 1, F_ONOFF); break;
            case 1: P(x, "Humanize", 1, &q->rg_human, 0, 100, F_PCT); break;
            case 2: P(x, "Ties", 1, &q->rg_ties, 0, 100, F_PCT); break;
            default: break;
            }
        break;
    case MN_EUCLID: {
        s4_trk_t *k = &q->d.multi.t[t];
        switch (i) {
        case 0: P(x, "Length", 1, &k->eu_len, 1, 64, F_NUM); break;
        case 1: P(x, "Pulses", 1, &k->eu_puls, 0, 64, F_NUM); break;
        case 2: P(x, "Rotate", 2, &k->eu_rot, -32, 32, F_SIGNED); break;
        default: P(x, "Invert", 1, &k->eu_inv, 0, 1, F_ONOFF); break;
        }
        break;
    }
    case MN_DRUMS: {
        s4_trk_t *k = &q->d.multi.t[t < 3 ? t : 0];
        switch (i) {
        case 0: P(x, t < 3 ? (t == 0 ? "Kick dens" : t == 1 ? "Snare dens" : "Hats dens") : "Kick dens", 1, &k->dg_dens, 0, 100, F_PCT); break;
        case 1: P(x, "X", 1, &q->dg_x, 0, 255, F_NUM); break;
        case 2: P(x, "Y", 1, &q->dg_y, 0, 255, F_NUM); break;
        default: P(x, "Chaos", 1, &q->dg_chaos, 0, 255, F_NUM); break;
        }
        break;
    }
    case MN_LFO: {
        s4_lfo_t *f = &q->lfo[pg / 3];
        switch (pg % 3) {
        case 0:
            switch (i) {
            case 0: P(x, "Wave", 1, &f->wave, 0, W_N - 1, F_WAVE); break;
            case 1: P(x, "Rate", 1, &f->rate, 0, 59, F_LRATE); break;
            case 2: P(x, "Amount", 1, &f->amt, 0, 100, F_NUM); break;
            default: P(x, "To", 1, &f->idest, 0, S4_D_N - 1, F_DEST); break;
            }
            break;
        case 1:
            switch (i) {
            case 0: P(x, "Phase", 1, &f->phase, 0, 127, F_NUM); break;
            case 1: P(x, "Offset", 2, &f->ofs, -127, 127, F_SIGNED); break;
            case 2: P(x, "Retrigger", 1, &f->retrig, 0, 1, F_ONOFF); break;
            default: P(x, "MIDI", 1, &f->xtype, 0, X_N - 1, F_XTYPE); break;
            }
            break;
        default:
            switch (i) {
            case 0: P(x, "CC", 1, &f->xnum, 0, 127, F_NUM); break;
            case 1: P(x, "MIDI amt", 1, &f->xamt, 0, 100, F_PCT); break;
            default: break;
            }
            break;
        }
        break;
    }
    case MN_ARP: {
        s4_arp_t *a = ui.view == V_KEYS || (q->mode != M_POLY && q->mode != M_CHORD) ? &q->karp : &q->arp;
        if (pg == 0)
            switch (i) {
            case 0: P(x, "Mode", 1, &a->mode, 0, A_N - 1, F_ARP); break;
            case 1: P(x, "Octaves", 2, &a->oct, -3, 3, F_SIGNED); break;
            case 2: P(x, "Gate", 1, &a->gate, 5, 100, F_PCT); break;
            default: P(x, "Division", 1, &a->div, DV_1BAR, DV_64, F_DIV); break;
            }
        else if (pg == 1)
            switch (i) {
            case 0: P(x, "Accent", 2, &a->accent, -100, 100, F_SIGNED); break;
            case 1: P(x, "Length", 1, &a->len, 1, 32, F_NUM); break;
            case 2: P(x, "Pulses", 1, &a->puls, 1, 32, F_NUM); break;
            default: P(x, "Repeats", 1, &a->rpt, 1, 8, F_NUM); break;
            }
        else
            switch (i) {
            case 0: P(x, "Rotate", 2, &a->rot, -16, 16, F_SIGNED); break;
            case 1: P(x, "Hold", 1, &a->hold, 0, 1, F_ONOFF); break;
            default: break;
            }
        break;
    }
    case MN_SEQ:
        switch (pg) {
        case 0:
            switch (i) {
            case 0: P(x, "Channel", 1, &q->ch, 0, 15, F_CH); break;
            case 1: P(x, "Port", 1, &q->port, 0, 3, F_PORT); break;
            case 2: P(x, "Program", 1, &q->pc, 0, 128, F_OFFNUM); break;
            default: P(x, "Mode", 1, &q->mode, 0, M_N - 1, F_MODE); break;
            }
            break;
        case 1:
            switch (i) {
            case 0: P(x, "Launch", 1, &q->punch, 0, 5, F_QUANT); break;
            case 1: P(x, "Reset bars", 1, &q->reset, 0, 64, F_OFFNUM); break;
            case 2: P(x, "Bank", 1, &q->bank, 0, 128, F_OFFNUM); break;
            default: P(x, "Transpose", 1, &q->tpose, 0, 17, F_TPOSE); break;
            }
            break;
        case 2:
            switch (i) {
            case 0: P(x, "Color", 1, &q->color, 0, 11, F_COLOR); break;
            case 1:
                if (q->mode == M_POLY)
                    P(x, "Voices", 1, &q->poly_max, 1, S4_POLY, F_NUM);
                else
                    P(x, "Leader", 1, &q->leader, 0, 3, F_SEQ);
                break;
            case 2: P(x, "Glide CC", 1, &q->glide_cc, 0, 1, F_ONOFF); break;
            default: P(x, "Follow", 1, &q->follow, 0, 1, F_ONOFF); break;
            }
            break;
        case 3:
            switch (i) {
            case 0: P(x, "Nudge", 2, &q->nudge, -100, 100, F_MS); break;
            case 1: P(x, "Time bend", 2, &q->bend, -50, 50, F_SPCT); break;
            case 2: P(x, "New vel", 1, &q->vdef, 1, 127, F_NUM); break;
            default: P(x, "New gate", 1, &q->gdef, 1, 100, F_PCT); break;
            }
            break;
        default:
            switch (i) {
            case 0: P(x, "Bank", 1, &q->ch_bank, 0, 1, F_BANK); break;
            case 1: P(x, "Auto voice", 1, &q->ch_auto, 0, 1, F_ONOFF); break;
            case 2: P(x, "Bass", 1, &q->ch_bass, 0, 1, F_ONOFF); break;
            default: P(x, "Spread all", 2, &q->cg_spread, -7, 7, F_SIGNED); break;
            }
            break;
        }
        break;
    case MN_TRACK: {
        s4_trk_t *k = &q->d.multi.t[t];
        switch (pg) {
        case 0:
            switch (i) {
            case 0: P(x, "Channel", 1, &k->ch, 0, 15, F_CH); break;
            case 1: P(x, "Port", 1, &k->port, 0, 3, F_PORT); break;
            case 2: P(x, "Note", 1, &k->note, 0, 127, F_NOTE); break;
            default: P(x, "Division", 1, &k->div, 0, DV_N - 1, F_DIV); break;
            }
            break;
        case 1:
            switch (i) {
            case 0: P(x, "Program", 1, &k->pc, 0, 128, F_OFFNUM); break;
            case 1: P(x, "Bank", 1, &k->bank, 0, 128, F_OFFNUM); break;
            case 2: P(x, "Glide", 1, &k->glide, 0, 100, F_PCT); break;
            default: P(x, "LFOs", 1, &k->lfo_on, 0, 3, F_LFOON); break;
            }
            break;
        default:
            switch (i) {
            case 0: P(x, "Random", 1, &k->rnd_on, 0, 1, F_ONOFF); break;
            case 1: P(x, "Direction", 1, &k->dir, 0, D_N - 1, F_DIR); break;
            case 2: P(x, "Start", 1, &k->init, 0, 127, F_NUM); break;
            default: P(x, "End", 1, &k->end, 0, 127, F_NUM); break;
            }
            break;
        }
        break;
    }
    case MN_MODSET: {
        s4_modl_t *ml = &q->ml[ui.lane & 7];
        switch (pg) {
        case 0:
            switch (i) {
            case 0: P(x, "Sends", 1, &ml->xtype, 0, X_N - 1, F_XTYPE); break;
            case 1: P(x, "CC", 1, &ml->xnum, 0, 127, F_NUM); break;
            case 2: P(x, "Min", 1, &ml->mn, 0, 120, F_NUM); break;
            default: P(x, "Max", 1, &ml->mx, 7, 127, F_NUM); break;
            }
            break;
        case 1:
            switch (i) {
            case 0: P(x, "To", 1, &ml->idest[0], 0, S4_D_N - 1, F_DEST); break;
            case 1: P(x, "Amount", 2, &ml->iamt[0], -100, 100, F_SIGNED); break;
            case 2: P(x, "Offset", 2, &ml->iofs[0], -127, 127, F_SIGNED); break;
            default: P(x, "Smooth", 1, &ml->smooth, 0, 30, F_NUM); break;
            }
            break;
        case 2:
            switch (i) {
            case 0: P(x, "To 2", 1, &ml->idest[1], 0, S4_D_N - 1, F_DEST); break;
            case 1: P(x, "Amount 2", 2, &ml->iamt[1], -100, 100, F_SIGNED); break;
            case 2: P(x, "Offset 2", 2, &ml->iofs[1], -127, 127, F_SIGNED); break;
            default: P(x, "Linked", 1, &ml->linked, 0, 1, F_ONOFF); break;
            }
            break;
        default:
            switch (i) {
            case 0: P(x, "Division", 1, &ml->div, 0, DV_N - 1, F_DIV); break;
            case 1: P(x, "Start", 1, &ml->init, 0, 127, F_NUM); break;
            case 2: P(x, "End", 1, &ml->end, 0, 127, F_NUM); break;
            default: P(x, "On", 1, &ml->on, 0, 1, F_ONOFF); break;
            }
            break;
        }
        break;
    }
    case MN_MAP: {
        s4_map_t *mp = &q->map[ui.map_i & 15];
        static uint8_t mi;
        mi = ui.map_i;
        switch (pg) {
        case 0:
            switch (i) {
            case 0: P(x, "Mapping", 1, &mi, 0, 15, F_NUM); x->g = 3; break;
            case 1: P(x, "Type", 1, &mp->type, 0, 3, F_MAPTYPE); break;
            case 2: P(x, "Channel", 1, &mp->ch, 0, 17, F_MAPCH); break;
            default: P(x, "Number", 1, &mp->num, 0, 127, F_NUM); break;
            }
            break;
        case 1: {
            static uint8_t learn;
            learn = ui.learn_in != 0;
            switch (i) {
            case 0: P(x, "Learn", 1, &learn, 0, 1, F_LEARN); x->g = 4; break;
            case 1: P(x, "Tracks", 1, &mp->trk, 0, 255, F_TRKS); break;
            case 2: P(x, "Send as CC", 1, &mp->fwd, 0, 128, F_OFFNUM); break;
            default: P(x, "Consume", 1, &mp->consume, 0, 1, F_ONOFF); break;
            }
            break;
        }
        case 2:
        case 3:
            switch (i) {
            case 0: P(x, pg == 2 ? "To" : "To 2", 1, &mp->idest[pg - 2], 0, S4_D_N - 1, F_DEST); break;
            case 1: P(x, "Amount", 2, &mp->iamt[pg - 2], -127, 127, F_SIGNED); break;
            case 2: P(x, "Offset", 2, &mp->iofs[pg - 2], -127, 127, F_SIGNED); break;
            default: break;
            }
            break;
        default:
            switch (i) {
            case 0: P(x, pg == 4 ? "Setting" : "Setting 2", 1, &mp->para[pg - 4], 0, S4_P_N - 1, F_PARA); break;
            case 1: P(x, "Amount", 2, &mp->pamt[pg - 4], -127, 127, F_SIGNED); break;
            case 2: P(x, "Offset", 2, &mp->pofs[pg - 4], -127, 127, F_SIGNED); break;
            default: break;
            }
            break;
        }
        break;
    }
    case MN_REC:
        if (pg == 0)
            switch (i) {
            case 0: PG(x, "Quantize", 1, &s4g.rec_q, 0, 1, F_ONOFF); break;
            case 1: PG(x, "Amount", 1, &s4g.rec_qpct, 0, 100, F_PCT); break;
            case 2: PG(x, "Overdub", 1, &s4g.rec_over, 0, 1, F_ONOFF); break;
            default: PG(x, "Mode", 1, &s4g.rec_mode, 0, 2, F_RECM); break;
            }
        else
            switch (i) {
            case 0: PG(x, "Count-in", 1, &s4g.count_in, 0, 4, F_COUNT); break;
            case 1: PG(x, "Metronome", 1, &s4g.met_mode, 0, 2, F_MET); break;
            case 2: PG(x, "Click level", 1, &s4g.met_vol, 0, 10, F_NUM); break;
            default: PG(x, "MIDI click", 1, &s4g.met_ch, 0, 16, F_OFFNUM); break;
            }
        break;
    case MN_SETUP:
        switch (pg) {
        case 0:
            switch (i) {
            case 0: PG(x, "Clock", 1, &s4g.clock, 0, 3, F_CLOCK); break;
            case 1: PG(x, "Clock out", 1, &s4g.clk_out, 0, 15, F_PORTS); break;
            case 2: PG(x, "Ignore start", 1, &s4g.start_ign, 0, 1, F_ONOFF); break;
            default: PG(x, "Patterns at", 1, &s4g.loadq, 1, 5, F_QUANT); break;
            }
            break;
        case 1:
            switch (i) {
            case 0: PG(x, "TRS in", 1, &s4g.thru, 0, 4, F_THRU); break;
            case 1: PG(x, "USB in", 1, &s4g.usb_thru, 0, 2, F_UTHRU); break;
            case 2: PG(x, "In channels", 1, &s4g.filt, 0, 2, F_NUM); break;
            default: PG(x, "To the scale", 1, &s4g.kq, 0, 1, F_ONOFF); break;
            }
            break;
        case 2:
            switch (i) {
            case 0: PG(x, "Program in", 1, &s4g.pc_ch, 0, 16, F_OFFNUM); break;
            case 1: PG(x, "Transport CC", 1, &s4g.tcc_ch, 0, 16, F_OFFNUM); break;
            case 2: PG(x, "Bank LSB", 1, &s4g.bank_lsb, 0, 1, F_ONOFF); break;
            default: PG(x, "Muted send", 1, &s4g.mute_cc, 0, 1, F_ONOFF); break;
            }
            break;
        case 3:
            switch (i) {
            case 0: PG(x, "PLAY", 1, &s4g.play_mode, 0, 2, F_PLAY); break;
            case 1: PG(x, "MUTE", 1, &s4g.mute_mode, 0, 3, F_MUTEM); break;
            case 2: PG(x, "Keep mutes", 1, &s4g.keep_mutes, 0, 1, F_ONOFF); break;
            default: PG(x, "Tap count", 1, &s4g.taps, 0, 8, F_OFFNUM); break;
            }
            break;
        case 4:
            switch (i) {
            case 0: PG(x, "Arp velocity", 1, &s4g.karp_vel, 0, 1, F_ONOFF); break;
            case 1: PG(x, "Hold adds", 1, &s4g.latch, 0, 1, F_ONOFF); break;
            case 2: PG(x, "Arp restarts", 1, &s4g.arp_reset, 0, 1, F_ONOFF); break;
            default: break;
            }
            break;
        case 5:
            switch (i) {
            case 0: PG(x, "Octave jump", 1, &s4g.harm_jump, 0, 4, F_HARMJ); break;
            case 1: PG(x, "Base octave", 1, &s4g.harm_base, 0, 1, F_ONOFF); break;
            case 2: PG(x, "Lock scale", 1, &s4g.lock_scale, 0, 1, F_ONOFF); break;
            default: break;
            }
            break;
        case 6:
            switch (i) {
            case 0: PG(x, "Velocity", 1, &s4g.vdef, 1, 127, F_NUM); break;
            case 1: PG(x, "Gate", 1, &s4g.gdef, 1, 100, F_PCT); break;
            case 2: PG(x, "Mod smooth", 1, &s4g.smooth, 0, 30, F_NUM); break;
            default: break;
            }
            break;
        case 7:
            switch (i) {
            case 0: PG(x, "Lights", 1, &s4g.leds, 0, 2, F_LEDS); break;
            case 1: PG(x, "Glow", 1, &s4g.glow, 0, 1, F_ONOFF); break;
            default: break;
            }
            break;
        default:
            break;
        }
        break;
    case MN_NUDGE:
        P(x, "Seq 1", 2, pg == 0 ? (void *)&s4[i].nudge : (void *)&s4[i].bend, pg == 0 ? -100 : -50, pg == 0 ? 100 : 50,
          pg == 0 ? F_MS : F_SPCT);
        x->label = pg == 0 ? (i == 0 ? "Nudge 1" : i == 1 ? "Nudge 2" : i == 2 ? "Nudge 3" : "Nudge 4")
                           : (i == 0 ? "Bend 1" : i == 1 ? "Bend 2" : i == 2 ? "Bend 3" : "Bend 4");
        break;
    case MN_LANE: {
        s4_mxtrk_t *mx = &q->d.mx[t & 3];
        s4_lane_t *ln = &mx->lane[ui.mx_lane];
        if (pg == 0)
            switch (i) {
            case 0: P(x, "Start", 1, &ln->init, 0, 15, F_NUM); break;
            case 1: P(x, "End", 1, &ln->end, 0, 15, F_NUM); break;
            case 2: P(x, "Division", 1, &ln->div, 0, DV_N - 1, F_DIV); break;
            default: P(x, "Direction", 1, &ln->dir, 0, D_N - 1, F_DIR); break;
            }
        else if (pg == 1)
            switch (i) {
            case 0: P(x, "Random", 1, &ln->rand, 0, 100, F_PCT); break;
            case 1: P(x, "Min", 2, &ln->min, -24, 0, F_SIGNED); break;
            case 2: P(x, "Max", 2, &ln->max, 0, 24, F_SIGNED); break;
            default: break;
            }
        else
            switch (i) {
            case 0: P(x, "Channel", 1, &mx->ch, 0, 15, F_CH); break;
            case 1: P(x, "Port", 1, &mx->port, 0, 3, F_PORT); break;
            case 2: P(x, "Note", 2, &mx->note, -24, 24, F_SIGNED); break;
            default: P(x, "Reset", 1, &mx->rlen, 0, 64, F_OFFNUM); break;
            }
        break;
    }
    case MN_SLOT: {
        s4_slot_t *sl = &s4song[proj.song % S4_NSONG].slot[ui.song_lane & 3][ui.slot % S4_NSLOT];
        switch (i) {
        case 0: P(x, "Pattern", 1, &sl->pat, 0, 17, F_PAT); x->g = 5; break;
        case 1: P(x, "Repeats", 1, &sl->rep, 0, 19, F_REPS); x->g = 5; break;
        case 2: P(x, "Program", 1, &sl->pc, 0, 129, F_PCSLOT); x->g = 5; break;
        default: P(x, "Bank", 1, &sl->bank, 0, 129, F_PCSLOT); x->g = 5; break;
        }
        break;
    }
    case MN_MASTER: {
        s4_song_t *sg = &s4song[proj.song % S4_NSONG];
        switch (i) {
        case 0: P(x, "Master", 1, &sg->master, 0, 4, F_OFFNUM); x->g = 5; break;
        case 1: P(x, "Launch", 1, &sg->launchq, 0, 8, F_LAUNCH); x->g = 5; break;
        case 2: PG(x, "Patterns at", 1, &s4g.loadq, 1, 5, F_QUANT); break;
        default: P(x, "Song", 1, &proj.song, 0, S4_NSONG - 1, F_NUM); x->g = 1; break;
        }
        break;
    }
    default:
        break;
    }
    return x->p != 0;
}

/* a value as words */
static const char *const DIR_N[D_N] = {"Forward", "Back", "Pendulum", "Ping-pong", "Random", "Drunk"};
static const char *const WAVE_N[W_N] = {"Sine", "Triangle", "Saw up", "Saw down", "Square", "Pulse", "S&H",
                                        "Smooth", "Exp up", "Exp down", "Stairs"};
static const char *const ARP_N[A_N] = {"Off", "Up", "Down", "Up-down", "Ends twice", "Inward", "Outward",
                                       "In-out", "Pinky", "Thumb", "Random", "Rand once", "As played"};
static const char *const MODE_N[M_N] = {"Mono", "Poly", "Chord", "Multitrack", "Stochastic", "Matriceal"};
static const char *const XT_N[X_N] = {"Off", "CC", "Bend", "Pressure", "Program"};
static const char *const COLOR_N[12] = {"Cyan", "Magenta", "Amber", "Green", "Violet", "Red", "Blue", "Yellow",
                                        "Pink", "Orange", "White", "Teal"};
static const char *const ROOT_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const VOI_N[6] = {"Close", "Open", "Open 1st", "Open 2nd", "1st inv", "2nd inv"};
static void cond_text(int c, char *b)
{
    static const char *const L[] = {"Pre", "Not pre", "First", "Not first", "Fixed", "Fill", "Not fill", "Locked"};
    char t[8];
    if (c <= 100) {
        itoa_u((uint32_t)c, b);
        scat(b, "%", 12);
        return;
    }
    if (c < S4_C_PRE) {
        int idx = c < S4_C_NAB ? c - S4_C_AB : c - S4_C_NAB, B, A, a = 1, bb = 2;
        for (B = 2; B <= 8; B++)
            for (A = 1; A <= B; A++)
                if (idx-- == 0) {
                    a = A;
                    bb = B;
                }
        b[0] = 0;
        if (c >= S4_C_NAB)
            scopy(b, "Not ", 12);
        scat(b, itoa_u((uint32_t)a, t), 12);
        scat(b, ":", 12);
        scat(b, itoa_u((uint32_t)bb, t), 12);
        return;
    }
    scopy(b, L[clampi(c - S4_C_PRE, 0, 7)], 12);
}
static void fmt_val(int fmt, int v, char *b)
{
    char t[12];
    b[0] = 0;
    switch (fmt) {
    case F_SIGNED: itoa_s(v, b); break;
    case F_PCT: itoa_u((uint32_t)v, b); scat(b, "%", 12); break;
    case F_SPCT: itoa_s(v, b); scat(b, "%", 12); break;
    case F_MS: itoa_s(v, b); scat(b, " ms", 12); break;
    case F_ONOFF: scopy(b, v ? "On" : "Off", 12); break;
    case F_CH: itoa_u((uint32_t)v + 1u, b); break;
    case F_PORT: b[0] = (char)('A' + v); b[1] = 0; break;
    case F_NOTE: s4_note_name(v, b); break;
    case F_DIV: scopy(b, s4_div_name(v), 12); break;
    case F_DIR: scopy(b, DIR_N[clampi(v, 0, D_N - 1)], 12); break;
    case F_SCALE: scopy(b, s4_scale_name(v), 12); break;
    case F_ROOT: scopy(b, ROOT_N[clampi(v, 0, 11)], 12); break;
    case F_BPM:
        itoa_u((uint32_t)v / 10u, b);
        scat(b, ".", 12);
        scat(b, itoa_u((uint32_t)v % 10u, t), 12);
        break;
    case F_DEST: scopy(b, s4_dest_name(v), 12); break;
    case F_PARA: scopy(b, s4_para_name(v), 12); break;
    case F_XTYPE: scopy(b, XT_N[clampi(v, 0, X_N - 1)], 12); break;
    case F_WAVE: scopy(b, WAVE_N[clampi(v, 0, W_N - 1)], 12); break;
    case F_LRATE: {
        static const char *const SY[10] = {"16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
        if (v < 10)
            scopy(b, SY[v], 12);
        else {
            int hz100 = (int)(lfo_hz_ui(v) * 100.0f);
            if (hz100 >= 1000)
                itoa_u((uint32_t)(hz100 / 100), b);
            else {
                itoa_u((uint32_t)(hz100 / 100), b);
                scat(b, ".", 12);
                scat(b, itoa_u((uint32_t)((hz100 % 100) / 10), t), 12);
                if (hz100 < 100)
                    scat(b, itoa_u((uint32_t)(hz100 % 10), t), 12);
            }
            scat(b, " Hz", 12);
        }
        break;
    }
    case F_ARP: scopy(b, ARP_N[clampi(v, 0, A_N - 1)], 12); break;
    case F_MODE: scopy(b, MODE_N[clampi(v, 0, M_N - 1)], 12); break;
    case F_COND: cond_text(v, b); break;
    case F_RTG:
        if (v & S4_SKIP)
            scopy(b, "Skip", 12);
        else {
            b[0] = 'x';
            itoa_u((uint32_t)(v & S4_RTG_N) + 1u, b + 1);
        }
        break;
    case F_GATE:
        if (v == S4_TIE)
            scopy(b, "Tie", 12);
        else if (v == S4_LEG)
            scopy(b, "Legato", 12);
        else {
            itoa_u((uint32_t)v, b);
            scat(b, "%", 12);
        }
        break;
    case F_OFFNUM:
        if (!v)
            scopy(b, "Off", 12);
        else
            itoa_u((uint32_t)v, b);
        break;
    case F_QUANT: {
        static const char *const Q[6] = {"Global", "Now", "1 bar", "2 bars", "4 bars", "8 bars"};
        scopy(b, Q[clampi(v, 0, 5)], 12);
        break;
    }
    case F_MAPTYPE: {
        static const char *const T[4] = {"Off", "CC", "Note on", "Note off"};
        scopy(b, T[clampi(v, 0, 3)], 12);
        break;
    }
    case F_MAPCH:
        if (v == 0)
            scopy(b, "Its own", 12);
        else if (v == 17)
            scopy(b, "Any", 12);
        else
            itoa_u((uint32_t)v, b);
        break;
    case F_COLOR: scopy(b, COLOR_N[clampi(v, 0, 11)], 12); break;
    case F_CLOCK: {
        static const char *const C[4] = {"Own", "USB", "TRS", "Any"};
        scopy(b, C[clampi(v, 0, 3)], 12);
        break;
    }
    case F_PORTS: {
        int k, n = 0;
        for (k = 0; k < 4; k++)
            if (v >> k & 1)
                b[n++] = (char)('A' + k);
        b[n] = 0;
        if (!n)
            scopy(b, "None", 12);
        break;
    }
    case F_THRU:
        if (!v)
            scopy(b, "Plays seq", 12);
        else {
            scopy(b, "To port ", 12);
            b[8] = (char)('A' + v - 1);
            b[9] = 0;
        }
        break;
    case F_UTHRU: {
        static const char *const U[3] = {"Used only", "Plays seq", "Echoed"};
        scopy(b, U[clampi(v, 0, 2)], 12);
        break;
    }
    case F_PLAY: {
        static const char *const PL[3] = {"Stop", "Pause", "Restart"};
        scopy(b, PL[clampi(v, 0, 2)], 12);
        break;
    }
    case F_MUTEM: {
        static const char *const MM[4] = {"Stop now", "Stop at off", "Mute now", "Mute at off"};
        scopy(b, MM[clampi(v, 0, 3)], 12);
        break;
    }
    case F_RECM: {
        static const char *const R[3] = {"Loop", "One shot", "Extend"};
        scopy(b, R[clampi(v, 0, 2)], 12);
        break;
    }
    case F_COUNT: {
        static const char *const CI[5] = {"Off", "1 beat", "Half bar", "1 bar", "2 bars"};
        scopy(b, CI[clampi(v, 0, 4)], 12);
        break;
    }
    case F_MET: {
        static const char *const MT[3] = {"Off", "Count-in", "Always"};
        scopy(b, MT[clampi(v, 0, 2)], 12);
        break;
    }
    case F_VOI: scopy(b, VOI_N[clampi(v, 0, 5)], 12); break;
    case F_BANK: scopy(b, v ? "B" : "A", 12); break;
    case F_LOOPR:
        if (!v)
            scopy(b, "Same", 12);
        else
            scopy(b, s4_div_name(v - 1), 12);
        break;
    case F_LANE: itoa_u((uint32_t)v + 1u, b); break;
    case F_SEQ: itoa_u((uint32_t)v + 1u, b); break;
    case F_PAT:
        if (v == S4_BLANK)
            scopy(b, "Blank", 12);
        else if (v > S4_BLANK)
            scopy(b, "Empty", 12);
        else {
            b[0] = 'P';
            itoa_u((uint32_t)v + 1u, b + 1);
        }
        break;
    case F_REPS:
        if (!v)
            scopy(b, "Loop", 12);
        else {
            b[0] = 'x';
            itoa_u((uint32_t)v, b + 1);
        }
        break;
    case F_PCSLOT:
        if (v == 0)
            scopy(b, "Pattern's", 12);
        else if (v == 129)
            scopy(b, "Off", 12);
        else
            itoa_u((uint32_t)v, b);
        break;
    case F_LAUNCH:
        if (!v)
            scopy(b, "Its end", 12);
        else {
            itoa_u((uint32_t)v, b);
            scat(b, v == 1 ? " bar" : " bars", 12);
        }
        break;
    case F_TRKS:
        if (!v)
            scopy(b, "Selected", 12);
        else if (v == 255)
            scopy(b, "All", 12);
        else
            itoa_u((uint32_t)v, b);
        break;
    case F_LFOON: {
        static const char *const LO[4] = {"None", "LFO 1", "LFO 2", "Both"};
        scopy(b, LO[clampi(v, 0, 3)], 12);
        break;
    }
    case F_DENS:
        if (v <= 100) {
            itoa_u((uint32_t)v, b);
            scat(b, "%", 12);
        } else {
            static const char *const D[4] = {"Humanize", "Order", "Pitch", "Invert"};
            scopy(b, D[clampi(v - 101, 0, 3)], 12);
        }
        break;
    case F_TPOSE:
        if (!v)
            scopy(b, "Off", 12);
        else if (v == 1)
            scopy(b, "Its own", 12);
        else {
            scopy(b, "Ch ", 12);
            scat(b, itoa_u((uint32_t)v - 1u, t), 12);
        }
        break;
    case F_LEARN: scopy(b, ui.learn_in ? "Listening" : "Off", 12); break;
    case F_HARMJ:
        if (!v)
            scopy(b, "Free", 12);
        else {
            itoa_u((uint32_t)v, b);
            scat(b, v == 1 ? " octave" : " octaves", 12);
        }
        break;
    case F_LEDS: {
        static const char *const LD[3] = {"Off", "Keys, buttons", "Keys"};
        scopy(b, LD[clampi(v, 0, 2)], 12);
        break;
    }
    default: itoa_u((uint32_t)(v < 0 ? -v : v), b); break;
    }
}

/* a menu's knob turned: the value, and what follows from it */
static void menu_knob(int k, int e)
{
    prm_t x;
    int old, v;
    if (!menu_prm(ui.menu, ui.page, k, &x))
        return;
    old = prm_get(&x);
    v = clampi(old + (int)accel(EN_K1 + k, e, x.hi - x.lo), x.lo, x.hi);
    if (x.g == 2) {                              /* the Multitrack track picked */
        proj.trk[sel()] = (uint8_t)v;
        dirty_g();
        return;
    }
    if (x.g == 3) {                              /* the mapping picked */
        ui.map_i = (uint8_t)v;
        return;
    }
    if (x.g == 4) {                              /* MIDI learn */
        ui.learn_in = v ? s4p.in + 1u : 0u;
        return;
    }
    if (v == old)
        return;
    if (ui.menu == MN_SEQ && ui.page == 0 && k == 3) {   /* the mode: a sequencer of another kind */
        s4_set_mode(sel(), v);
        say("MODE", MODE_N[v]);
        dirty();
        return;
    }
    if (ui.menu == MN_EUCLID && !ui.menu_undo) {   /* (one undo for the menu's turns) */
        undo();
        ui.menu_undo = 1;
    }
    prm_set(&x, v);
    if (x.g == 1)
        dirty_g();
    else if (x.g == 5)
        project_dirty_songs();
    else
        dirty();
    if (ui.menu == MN_EUCLID)                    /* Euclid writes the track as it turns */
        s4_euclid(sel(), trk());
    if (ui.menu == MN_ARP && !v && ((ui.page == 0 && k == 0) || (ui.page == 2 && k == 1)))
        s4_arp_clear(sel());                     /* the arp (or hold) off: the notes it held let go */
}

static void set_trk(int t);
/* GX-1's GEN page (ui_gen.c): what ui_input.c and ui_draw.c call of it */
static void gen_key_down(int k);
static void gen_key_up(int k);
static int gen_btn_down(int b);
static int gen_btn_tap(int b);
static void gen_enc(int role, int32_t e);
static void gen_knob(int k, int32_t e);
static void gen_service(void);
static void gen_leds(uint32_t *bp, uint32_t *kp);
static void gen_init(void);
static void draw_gen(void);
static uint8_t ui_boot_view = V_GEN;       /* the view at start (the host simulator's SEQ-4 scenarios: the grid) */
static int gen_left(void);
#include "ui_input.c"
#include "ui_draw.c"
#include "ui_gen.c"
