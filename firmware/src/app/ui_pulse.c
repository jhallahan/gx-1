/* SPDX-License-Identifier: GPL-3.0-only */
/* GX-1's Pulse mode: the TS-1 pulse sequencer (Pulse, the phone player's rings) on the FM-1. GLO + SEQ switches the
 * whole box between GEN and Pulse; one plays at a time (GX-1's parts run on unheard, so they come back in place).
 * Sixteen tracks, each a ring of steps with its pulses spread evenly: 1-8 the Tonverk kit (channel 1, notes 60-67),
 * 9-10 bass (5), 11-12 chords (7), 13-16 leads (8). Part of ui.c.
 *
 *   white keys        choose a track          GLO + white key   that track on / off
 *   K1-K4             the page's four          SEL               the next page (Rhythm, Feel, Sound, Melody)
 *   ALGORITHM         the track                PRESETS           the scale
 *   OCT- / OCT+       the key                  SELECT            the tempo
 *   ENV               the track on / off       FX                new pulses for the track
 *   EDIT              the track back to its start (cleared)
 *   GLO + SEQ         back to GEN              PLAY              play / stop (as everywhere) */
#include "ts1/ts1_fm1.h"

static struct {
    uint8_t sel, pg, init, drawn;
    uint32_t sig[3];
} pu;

static const int8_t PU_SIN[256] = {0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46,49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88,90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116,117,118,120,121,122,122,123,124,125,125,126,126,126,127,127,127,127,127,127,127,126,126,126,125,125,124,123,122,122,121,120,118,117,116,115,113,112,111,109,107,106,104,102,100,98,96,94,92,90,88,85,83,81,78,76,73,71,68,65,63,60,57,54,51,49,46,43,40,37,34,31,28,25,22,19,16,12,9,6,3,0,-3,-6,-9,-12,-16,-19,-22,-25,-28,-31,-34,-37,-40,-43,-46,-49,-51,-54,-57,-60,-63,-65,-68,-71,-73,-76,-78,-81,-83,-85,-88,-90,-92,-94,-96,-98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,-117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,-127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,-117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100,-98,-96,-94,-92,-90,-88,-85,-83,-81,-78,-76,-73,-71,-68,-65,-63,-60,-57,-54,-51,-49,-46,-43,-40,-37,-34,-31,-28,-25,-22,-19,-16,-12,-9,-6,-3};
static int32_t pu_sin(uint32_t ph) { return PU_SIN[ph & 255u]; }
static int32_t pu_cos(uint32_t ph) { return PU_SIN[(ph + 64u) & 255u]; }

static const char *const PU_NAME[16] = {"KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "RIDE", "PERC 1", "PERC 2",
                                        "BASS 1", "BASS 2", "CHORDS 1", "CHORDS 2", "LEAD 1", "LEAD 2", "LEAD 3", "LEAD 4"};
static const uint8_t PU_PAGE[4][4] = {
    {TS1_P_STEPS, TS1_P_PULSES, TS1_P_ROTATE, TS1_P_DIV},
    {TS1_P_REPEATS, TS1_P_ACCENT, TS1_P_PROB, TS1_P_TEMP},
    {TS1_P_NOTE, TS1_P_CH, TS1_P_VEL, TS1_P_SUSTAIN},
    {TS1_P_PHRASE, TS1_P_RANGE, TS1_P_VOICES, TS1_P_FOLLOW}};
static const char *const PU_PGNAME[4] = {"RHYTHM", "FEEL", "SOUND", "MELODY"};
static const char *const PU_DIV[TS1_NDIV] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"};
static const char *const PU_PHRASE[TS1_NPHRASE] = {"Hold", "Up", "Down", "Up-down", "Random", "Walk", "Chord"};
static const char *const PU_SCALE[TS1_NSCALE] = {"MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN",
                                                 "HARM MINOR", "PENTA MAJOR", "PENTA MINOR", "BLUES", "CHROMATIC"};

static int pu_get(int t, int p) { return ts1_get(&s4_ps, t, p); }
static void pu_set(int t, int p, int v) { ts1_set(&s4_ps, t, p, v); }

/* the start: the Tonverk kit on channel 1 (notes 60-67, as GX-1's drums), bass 5, chords 7, leads 8; a kick, a
   backbeat, hats and a bass line playing */
static void pu_defaults(void)
{
    int t;
    ts1_init(&s4_ps);
    for (t = 0; t < 16; t++) {
        pu_set(t, TS1_P_ON, 0);
        if (t < 8) {
            pu_set(t, TS1_P_KIND, 0);
            pu_set(t, TS1_P_CH, 0);
            pu_set(t, TS1_P_NOTE, 60 + t);
        } else {
            pu_set(t, TS1_P_KIND, 1);
            pu_set(t, TS1_P_CH, t < 10 ? 4 : t < 12 ? 6 : 7);
            pu_set(t, TS1_P_NOTE, t < 10 ? 36 : t < 12 ? 48 : 60);
            if (t == 10 || t == 11) {
                pu_set(t, TS1_P_VOICES, 3);
                pu_set(t, TS1_P_SUSTAIN, 100);
                pu_set(t, TS1_P_PHRASE, TS1_PH_HOLD);
            }
        }
    }
    pu_set(0, TS1_P_ON, 1); pu_set(0, TS1_P_PULSES, 4);
    pu_set(1, TS1_P_ON, 1); pu_set(1, TS1_P_PULSES, 2); pu_set(1, TS1_P_ROTATE, 4);
    pu_set(3, TS1_P_ON, 1); pu_set(3, TS1_P_PULSES, 11); pu_set(3, TS1_P_ACCENT, 4); pu_set(3, TS1_P_VEL, 70);
    pu_set(8, TS1_P_ON, 1); pu_set(8, TS1_P_PULSES, 5); pu_set(8, TS1_P_ROTATE, 2); pu_set(8, TS1_P_RANGE, 5);
    pu_set(8, TS1_P_PHRASE, TS1_PH_UP);
}

static void pu_enter(void)
{
    if (!pu.init) {
        pu_defaults();
        pu.init = 1;
    }
    s4_panic();                                  /* GX-1's notes end; its parts run on, unheard */
    s4_pulse_on = 1;
    set_view(V_PULSE);
    pu.drawn = 0;
    say("PULSE", "GLO+SEQ: GEN");
}
static void pu_leave(void)
{
    s4_pulse_on = 0;
    s4_panic();
    set_view(V_GEN);
    say("GEN", "GLO+SEQ: PULSE");
}
static int pulse_left(void)
{
    int was = pu.drawn;
    pu.drawn = 0;
    return was;
}

static void pu_track_text(char *t, int r)
{
    char n[8];
    itoa_u((uint32_t)r + 1u, n);
    scopy(t, n, 24);
    scat(t, " ", 24);
    scat(t, PU_NAME[r & 15], 24);
}
static void pu_value_text(char *t, int r, int p)
{
    int v = pu_get(r, p);
    char n[8];
    switch (p) {
    case TS1_P_DIV: scopy(t, PU_DIV[v < 0 ? 0 : v % TS1_NDIV], 12); return;
    case TS1_P_PHRASE: scopy(t, pu_get(r, TS1_P_KIND) ? PU_PHRASE[v % TS1_NPHRASE] : "-", 12); return;
    case TS1_P_FOLLOW: scopy(t, !pu_get(r, TS1_P_KIND) ? "-" : v ? "On" : "Off", 12); return;
    case TS1_P_RANGE: case TS1_P_VOICES:
        if (!pu_get(r, TS1_P_KIND)) {
            scopy(t, "-", 12);
            return;
        }
        break;
    case TS1_P_CH: v++; break;
    case TS1_P_NOTE:
        scopy(t, GN_NOTE[v % 12], 12);
        itoa_s(v / 12 - 1, n);
        scat(t, v / 12 - 1 > 0 ? n + 1 : n, 12);   /* C4 = 60 */
        return;
    case TS1_P_PROB: case TS1_P_SUSTAIN: case TS1_P_TEMP:
        itoa_u((uint32_t)v, t);
        scat(t, "%", 12);
        return;
    default: break;
    }
    if (v >= 0)
        itoa_u((uint32_t)v, t);
    else
        itoa_s(v, t);
}
static const char *pu_param_label(int p)
{
    switch (p) {
    case TS1_P_PROB: return "Chance";
    case TS1_P_TEMP: return "Loose";
    case TS1_P_SUSTAIN: return "Length";
    case TS1_P_FOLLOW: return "Chords";
    default: return ts1_param_name[p];
    }
}

/* where a track is in its ring now: its step, or -1 stopped */
static int pu_step(int r)
{
    uint32_t D = ts1_div_ticks[pu_get(r, TS1_P_DIV) % TS1_NDIV];
    int steps = pu_get(r, TS1_P_STEPS);
    if (!s4p.on || steps < 1)
        return -1;
    return (int)((s4_pulse_pos / D) % (uint32_t)steps);
}
/* the rings shown: the tracks on, and the one chosen; outermost first */
static int pu_rings(uint8_t *list)
{
    int r, n = 0;
    for (r = 0; r < 16; r++)
        if (pu_get(r, TS1_P_ON) || r == pu.sel)
            list[n++] = (uint8_t)r;
    return n;
}
static uint16_t pu_colour(int r)
{
    return r < 8 ? RGB(251, 191, 36) : r < 10 ? RGB(248, 113, 113) : r < 12 ? RGB(74, 222, 128) : GN_ACC;
}
static void pu_paint_rings(void)
{
    uint8_t list[16];
    int n = pu_rings(list), i, k;
    int32_t cx = 120, cy = 98, R = 74, sp = n > 1 ? (R - 14) / (n - 1) : 0;
    if (sp > 12)
        sp = 12;
    for (i = 0; i < n; i++) {                    /* the rings: a faint circle each */
        int32_t rad = R - i * sp;
        uint16_t c = list[i] == pu.sel ? RGB(70, 70, 80) : RGB(36, 36, 42);
        for (k = 0; k < 512; k++) {
            uint32_t a = (uint32_t)k >> 1;
            int32_t sx = (k & 1) ? (pu_sin(a) + pu_sin(a + 1u)) : 2 * pu_sin(a);
            int32_t cy2 = (k & 1) ? (pu_cos(a) + pu_cos(a + 1u)) : 2 * pu_cos(a);
            cv_pset(cx + sx * rad / 254, cy - cy2 * rad / 254, c);
        }
    }
    for (i = 0; i < n; i++) {
        int r = list[i], steps = pu_get(r, TS1_P_STEPS), pulses = pu_get(r, TS1_P_PULSES), rot = pu_get(r, TS1_P_ROTATE);
        int acc = pu_get(r, TS1_P_ACCENT), on = pu_get(r, TS1_P_ON), ph = pu_step(r), idx = 0, chosen = r == pu.sel;
        int32_t rad = R - i * sp;
        uint16_t col = !on ? GN_OFF : chosen ? pu_colour(r) : GN_MUTED;
        for (k = 0; k < steps; k++) {
            uint32_t a = (uint32_t)(k * 256 / steps);
            int32_t x = cx + pu_sin(a) * rad / 127, y = cy - pu_cos(a) * rad / 127;
            int hit = ts1_pulse(steps, pulses, rot, k), big;
            uint16_t c = col;
            if (hit) {
                big = (sp >= 9 || n == 1 ? 2 : 1) + (acc && ts1_pulse(pulses, acc, 0, idx) ? 1 : 0);
                idx++;
            } else
                big = 0;
            if (k == ph) {
                c = GN_INK;
                if (!big)
                    big = 1;
            }
            if (big)
                cv_rect(x - big, y - big, 2 * big + 1, 2 * big + 1, c);
            else if (chosen)
                cv_pset(x, y, GN_MUTED);
        }
    }
    if (n == 1 || !pu_get(pu.sel, TS1_P_ON)) {
        char t[8];
        itoa_u((uint32_t)pu.sel + 1u, t);
        text_c(cx, cy - 8, &FONT_B, t, GN_INK);
    }
}
static void pu_paint_top(void)
{
    char t[32], n[8];
    int msg = ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0;
    cv_rect(0, 0, 240, 20, GN_CHROME);
    cv_text(4, 2, &FONT_XS, "PULSE", GN_ACC);
    if (msg) {
        scopy(t, ui.msg[0], 32);
        if (ui.msg[1][0]) {
            scat(t, " ", 32);
            scat(t, ui.msg[1], 32);
        }
        text_r(236, 2, &FONT_XS, t, GN_INK);
        return;
    }
    scopy(t, GN_NOTE[s4_ps.s[TS1_S_ROOT] % 12], 32);
    scat(t, " ", 32);
    scat(t, PU_SCALE[s4_ps.s[TS1_S_SCALE] % TS1_NSCALE], 32);
    cv_text(52, 2, &FONT_XS, t, GN_INK);
    itoa_u(s4g.tempo / 10u, n);
    text_r(236, 2, &FONT_XS, n, s4p.ext ? RGB(96, 165, 250) : GN_INK);
}
static void pu_paint_bottom(void)
{
    char t[24];
    int k, r = pu.sel;
    pu_track_text(t, r);
    cv_text(6, 178, &FONT_S, t, pu_get(r, TS1_P_ON) ? pu_colour(r) : GN_MUTED);
    scopy(t, PU_PGNAME[pu.pg], 24);
    scat(t, pu_get(r, TS1_P_ON) ? "   ch " : "   OFF  ch ", 24);
    {
        char n[8];
        itoa_u((uint32_t)pu_get(r, TS1_P_CH) + 1u, n);
        scat(t, n, 24);
    }
    text_r(234, 179, &FONT_XS, t, GN_MUTED);
    cv_rect(0, 196, 240, 44, GN_EDIT);
    for (k = 0; k < 4; k++) {
        int32_t x = k * 60 + 30;
        int p = PU_PAGE[pu.pg][k];
        pu_value_text(t, r, p);
        text_c(x, 198, &FONT_XS, pu_param_label(p), GN_MUTED);
        text_c(x, 216, &FONT_S, t, GN_INK);
    }
    for (k = 0; k < 4; k++)                      /* the page: four dots */
        cv_rect(104 + k * 9, 236, 5, 2, k == pu.pg ? GN_ACC : GN_LINE);
}
static void pu_band(int32_t y0, int32_t h, void (*paint)(void))
{
    cv_begin(240, (uint32_t)h, GN_BG);
    cv_oy = -y0;
    paint();
    cv_blit(0, (uint32_t)y0);
}
static void draw_pulse(void)
{
    uint8_t list[16];
    uint32_t h;
    int n, i, msg = ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0;
    if (!pu.drawn) {
        pu.sig[0] = pu.sig[1] = pu.sig[2] = 0;
        pu.drawn = 1;
    }
    h = hash(hash(hash(11u, s4g.tempo), (uint32_t)s4_ps.s[TS1_S_ROOT] << 8 | (uint32_t)s4_ps.s[TS1_S_SCALE]),
             (uint32_t)s4p.ext);
    h = hash(h, msg ? hash_s(hash_s(5u, ui.msg[0]), ui.msg[1]) : 0u);
    if (h != pu.sig[0]) {
        pu.sig[0] = h;
        pu_band(0, 20, pu_paint_top);
    }
    n = pu_rings(list);
    h = hash(hash(12u, pu.sel), (uint32_t)n);
    for (i = 0; i < n; i++) {
        int r = list[i], p;
        for (p = 0; p < 8; p++)
            h = hash(h, (uint32_t)pu_get(r, p));
        h = hash(hash(h, (uint32_t)pu_get(r, TS1_P_ACCENT)), (uint32_t)(pu_step(r) + 1));
    }
    if (h != pu.sig[1]) {
        pu.sig[1] = h;
        pu_band(20, 78, pu_paint_rings);
        pu_band(98, 78, pu_paint_rings);
    }
    h = hash(hash(hash(13u, pu.sel), pu.pg), (uint32_t)pu_get(pu.sel, TS1_P_KIND));
    for (i = 0; i < (int)TS1_NPARAM; i++)
        h = hash(h, (uint32_t)pu_get(pu.sel, i));
    if (h != pu.sig[2]) {
        pu.sig[2] = h;
        pu_band(176, 64, pu_paint_bottom);
    }
}

/* ---- the panel ---- */

static void pu_choose(int r)
{
    char t[24];
    pu.sel = (uint8_t)clampi(r, 0, 15);
    pu_track_text(t, pu.sel);
    say(t, pu_get(pu.sel, TS1_P_ON) ? "" : "OFF");
}
static void pu_toggle(int r)
{
    char t[24];
    pu_set(r, TS1_P_ON, !pu_get(r, TS1_P_ON));
    if (pu_get(r, TS1_P_ON) && pu_get(r, TS1_P_PULSES) < 1)
        pu_set(r, TS1_P_PULSES, 4);              /* on, with something to play */
    pu_track_text(t, r);
    say(t, pu_get(r, TS1_P_ON) ? "ON" : "OFF");
}
static void pulse_key_down(int k)
{
    int w = white_of(k);
    if (w < 0)
        return;
    if (shift()) {
        ui.btn_used |= 1u << B_GLO;
        pu_toggle(w);
    } else
        pu_choose(w);
}
static int pulse_btn_down(int b)
{
    if (b == B_PLAY || b == B_GLO)
        return 0;                                /* (PLAY as everywhere; GLO is the shift) */
    if (b == B_SEQ && shift()) {
        ui.btn_used |= 1u << b | 1u << B_GLO;
        pu_leave();
        return 1;
    }
    if (b == B_OCTDN || b == B_OCTUP) {
        int v = s4_ps.s[TS1_S_ROOT] + (b == B_OCTUP ? 1 : -1);
        s4_ps.s[TS1_S_ROOT] = (int16_t)((v + 12) % 12);
        ui.btn_used |= 1u << b;
        say("KEY", GN_NOTE[s4_ps.s[TS1_S_ROOT]]);
    }
    return 1;
}
static int pulse_btn_tap(int b)
{
    int r = pu.sel;
    char t[24];
    switch (b) {
    case B_SEL: pu.pg = (uint8_t)((pu.pg + (shift() ? 3 : 1)) % 4); say(PU_PGNAME[pu.pg], ""); break;
    case B_ENV: pu_toggle(r); break;
    case B_FX:                                   /* new pulses: how many, and where they start */
        pu_set(r, TS1_P_PULSES, 1 + (int)(s4_rand() % (uint32_t)pu_get(r, TS1_P_STEPS)));
        pu_set(r, TS1_P_ROTATE, (int)(s4_rand() % (uint32_t)pu_get(r, TS1_P_STEPS)));
        if (!pu_get(r, TS1_P_ON))
            pu_set(r, TS1_P_ON, 1);
        pu_track_text(t, r);
        say(t, "NEW PULSES");
        break;
    case B_EDIT:
        pu_set(r, TS1_P_PULSES, 0);
        pu_set(r, TS1_P_ROTATE, 0);
        pu_set(r, TS1_P_ON, 0);
        pu_track_text(t, r);
        say(t, "CLEARED");
        break;
    default: break;
    }
    return b != B_PLAY;
}
static void pulse_knob(int k, int32_t e)
{
    int p = PU_PAGE[pu.pg][k & 3], r = pu.sel, v = pu_get(r, p), d;
    char t[12];
    if (p == TS1_P_PHRASE || p == TS1_P_RANGE || p == TS1_P_VOICES || p == TS1_P_FOLLOW) {
        if (!pu_get(r, TS1_P_KIND)) {
            say(PU_NAME[r], "A DRUM: NO MELODY");
            return;
        }
    }
    d = p == TS1_P_DIV || p == TS1_P_PHRASE || p == TS1_P_FOLLOW || p == TS1_P_CH || p == TS1_P_STEPS ||
                p == TS1_P_PULSES || p == TS1_P_ROTATE || p == TS1_P_REPEATS || p == TS1_P_ACCENT ||
                p == TS1_P_RANGE || p == TS1_P_VOICES || p == TS1_P_NOTE
            ? (e > 0 ? 1 : -1)
            : (int)accel(EN_K1 + k, e, 127);
    pu_set(r, p, v + d);
    pu_value_text(t, r, p);
    say(pu_param_label(p), t);
}
static void pulse_enc(int role, int32_t e)
{
    int d = e > 0 ? 1 : -1;
    char t[12];
    switch (role) {
    case EN_ALGO: pu_choose((pu.sel + 16 + d) % 16); break;
    case EN_PRESET:
        s4_ps.s[TS1_S_SCALE] = (int16_t)((s4_ps.s[TS1_S_SCALE] + TS1_NSCALE + d) % TS1_NSCALE);
        say("SCALE", PU_SCALE[s4_ps.s[TS1_S_SCALE]]);
        break;
    case EN_SELECT: {
        int v = clampi((int)s4g.tempo + (int)accel(EN_SELECT, e, 100) * 10, 600, 2000);
        s4g.tempo = (uint16_t)v;
        dirty_g();
        itoa_u((uint32_t)v / 10u, t);
        say("TEMPO", t);
        break;
    }
    default: break;
    }
}
static void pulse_leds(uint32_t *bp, uint32_t *kp)
{
    uint32_t k = *kp;
    int i;
    for (i = 0; i < 16; i++) {
        k &= ~(1u << WHITE_KEY[i]);
        if (i == pu.sel ? blink() : pu_get(i, TS1_P_ON))
            k |= 1u << WHITE_KEY[i];
    }
    for (i = 0; i < 11; i++)
        k &= ~(1u << BLACK_KEY[i]);
    *kp = k;
    (void)bp;
}
