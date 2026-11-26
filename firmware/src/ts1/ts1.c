/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TS-1: the pulse sequencer's core (ts1.h). */
#include "ts1.h"

const uint16_t ts1_div_ticks[TS1_NDIV] = {12, 16, 24, 32, 48, 64, 96, 192, 384};
const char *const ts1_param_name[TS1_NPARAM] = {"On", "Kind", "Channel", "Note", "Steps", "Pulses", "Rotate", "Division",
    "Sustain", "Repeats", "Pace", "Accent", "Velocity", "Accent +", "Groove", "Chance", "Temperature", "Phrase",
    "Range", "Degree", "Voices", "Spread", "Seed", "Follow chords", "Melody length", "Accent pitch"};
const char *const ts1_scale_name[TS1_NSCALE] = {"Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Harmonic minor", "Pentatonic major", "Pentatonic minor", "Blues", "Chromatic"};

static const int8_t SCALES[TS1_NSCALE][13] = {   /* [0] the notes in an octave, then their semitones */
    {7, 0, 2, 4, 5, 7, 9, 11}, {7, 0, 2, 3, 5, 7, 8, 10}, {7, 0, 2, 3, 5, 7, 9, 10}, {7, 0, 1, 3, 5, 7, 8, 10},
    {7, 0, 2, 4, 6, 7, 9, 11}, {7, 0, 2, 4, 5, 7, 9, 10}, {7, 0, 2, 3, 5, 7, 8, 11}, {5, 0, 2, 4, 7, 9},
    {5, 0, 3, 5, 7, 10}, {6, 0, 3, 5, 6, 7, 10}, {12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
/* each parameter's range ([lo, hi]; steps, pulses, rotate and accent also follow the track) */
static const int16_t LO[TS1_NPARAM] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, -3, 0, 1, 0, 50, 0, 0, 0, 1, -7, 1, 0, 0, 0, 0, 0};
static const int16_t HI[TS1_NPARAM] = {1, 1, 15, 127, 64, 64, 63, TS1_NDIV - 1, 400, 8, 3, 64, 127, 64, 75, 100, 100,
                                       TS1_NPHRASE - 1, 16, 14, 4, 1, 255, 1, 16, 3};
/* the chord progressions, as the scale degree each chord stands on (0 the key's root): so they fit any scale */
static const int8_t PROGS[TS1_NPROG][9] = {   /* [0] how many chords, then their degrees */
    {4, 0, 5, 2, 6}, {4, 0, 3, 4, 0}, {4, 0, 4, 5, 3}, {4, 5, 3, 0, 4},
    {4, 1, 4, 0, 0}, {4, 0, 6, 5, 6}, {2, 0, 3}, {2, 0, 4}};
const char *const ts1_prog_name[TS1_NPROG + 1] = {"None", "1-6-3-7", "1-4-5-1", "1-5-6-4", "6-4-1-5",
    "2-5-1-1", "1-7-6-7", "1-4", "1-5"};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du ^ d * 0x27D4EB2Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

int ts1_lo(int p, const ts1_song_t *s, int t) { (void)s; (void)t; return p >= 0 && p < (int)TS1_NPARAM ? LO[p] : 0; }
int ts1_hi(int p, const ts1_song_t *s, int t)
{
    const int16_t *v;
    if (p < 0 || p >= (int)TS1_NPARAM) return 0;
    v = s->trk[clampi(t, 0, TS1_TRACKS - 1)].v;
    if (p == TS1_P_PULSES) return v[TS1_P_STEPS];
    if (p == TS1_P_ROTATE) return v[TS1_P_STEPS] - 1;
    if (p == TS1_P_ACCENT) return v[TS1_P_PULSES];
    return HI[p];
}
int ts1_get(const ts1_song_t *s, int t, int p)
{
    if (t < 0 || t >= (int)TS1_TRACKS || p < 0 || p >= (int)TS1_NPARAM) return 0;
    return s->trk[t].v[p];
}
void ts1_set(ts1_song_t *s, int t, int p, int x)
{
    int16_t *v;
    if (t < 0 || t >= (int)TS1_TRACKS || p < 0 || p >= (int)TS1_NPARAM) return;
    v = s->trk[t].v;
    v[p] = (int16_t)clampi(x, ts1_lo(p, s, t), ts1_hi(p, s, t));
    /* what follows the steps and the pulses */
    v[TS1_P_PULSES] = (int16_t)clampi(v[TS1_P_PULSES], 0, v[TS1_P_STEPS]);
    v[TS1_P_ROTATE] = (int16_t)clampi(v[TS1_P_ROTATE], 0, v[TS1_P_STEPS] - 1);
    v[TS1_P_ACCENT] = (int16_t)clampi(v[TS1_P_ACCENT], 0, v[TS1_P_PULSES]);
}

void ts1_init(ts1_song_t *s)
{
    static const int16_t DEF[TS1_NPARAM] = {0, 0, 9, 36, 16, 0, 0, TS1_DIV_16, 50, 1, 0, 0, 100, 20, 50, 100, 0,
                                            TS1_PH_UP, 7, 0, 1, 0, 0, 1, 0, 0};
    static const int8_t DRUM[16] = {36, 38, 42, 46, 39, 37, 45, 49,    /* kick, snare, hats, clap, rim, tom, crash */
                                    35, 40, 44, 51, 70, 56, 41, 75};   /* (17..24) kick 2, snare 2, pedal hat, ride, shaker, cowbell, low tom, clave */
    unsigned t, p;
    for (t = 0; t < TS1_TRACKS; t++) {
        for (p = 0; p < TS1_NPARAM; p++) s->trk[t].v[p] = DEF[p];
        s->trk[t].v[TS1_P_SEED] = (int16_t)t;
        if (t % 16u < 8u) s->trk[t].v[TS1_P_NOTE] = DRUM[(t / 16u) * 8u + t % 8u];   /* drums: 1..8 and 17..24 */
        else {                                                                     /* melodic: 9..16 and 25..32 */
            s->trk[t].v[TS1_P_KIND] = 1;
            s->trk[t].v[TS1_P_CH] = (int16_t)((t % 8u) + (t / 16u) * 8u);
            s->trk[t].v[TS1_P_NOTE] = (int16_t)(t % 8u < 2u ? 36 : t % 8u < 4u ? 48 : 60);   /* bass 2, chords 2, lead 4 */
            s->trk[t].v[TS1_P_SUSTAIN] = 80;
        }
    }
    s->s[TS1_S_TEMPO] = 1200;
    s->s[TS1_S_ROOT] = 9;
    s->s[TS1_S_SCALE] = TS1_SC_MINOR;
    s->s[TS1_S_PROG] = 0;
    s->s[TS1_S_BPC] = 1;
    s->s[TS1_S_SWING] = 50;
    /* a start: a kick, a backbeat, hats, and a bass line */
    ts1_set(s, 0, TS1_P_ON, 1); ts1_set(s, 0, TS1_P_PULSES, 4);
    ts1_set(s, 1, TS1_P_ON, 1); ts1_set(s, 1, TS1_P_PULSES, 2); ts1_set(s, 1, TS1_P_ROTATE, 4);
    ts1_set(s, 2, TS1_P_ON, 1); ts1_set(s, 2, TS1_P_PULSES, 11); ts1_set(s, 2, TS1_P_ACCENT, 4); ts1_set(s, 2, TS1_P_VEL, 70);
    ts1_set(s, 8, TS1_P_ON, 1); ts1_set(s, 8, TS1_P_PULSES, 5); ts1_set(s, 8, TS1_P_ROTATE, 2); ts1_set(s, 8, TS1_P_RANGE, 5);
}

int ts1_pulse(int steps, int pulses, int rotate, int step)
{
    int i;
    if (steps < 1 || pulses < 1) return 0;
    i = ((step - rotate) % steps + steps) % steps;
    return (i * pulses) % steps < pulses;                  /* Bresenham: the pulses as even as they can be */
}
int ts1_scale_note(int sc, int d)
{
    const int8_t *S = SCALES[clampi(sc, 0, TS1_NSCALE - 1)];
    int n = S[0], o = d >= 0 ? d / n : -((-d + n - 1) / n);
    return o * 12 + S[1 + (d - o * n)];
}

int ts1_prog_len(int p)
{
    return p < 1 || p > (int)TS1_NPROG ? 1 : PROGS[p - 1][0];
}

int ts1_chord_degree(const ts1_song_t *s, uint32_t tick)
{
    int p = s->s[TS1_S_PROG], bpc = s->s[TS1_S_BPC];
    uint32_t bar = tick / (4u * TS1_PPQ);
    if (p < 1 || p > (int)TS1_NPROG) return 0;
    if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8) bpc = 1;
    return PROGS[p - 1][1 + (bar / (uint32_t)bpc) % (uint32_t)PROGS[p - 1][0]];
}

/* a melodic track's degree for its n-th pulse */
static int phrase_degree(const int16_t *v, uint32_t n, uint32_t cycle, uint32_t idx)
{
    int r = v[TS1_P_RANGE], d = 0, k;
    uint32_t h;
    switch (v[TS1_P_PHRASE]) {
    case TS1_PH_UP: d = (int)(n % (uint32_t)r); break;
    case TS1_PH_DOWN: d = r - 1 - (int)(n % (uint32_t)r); break;
    case TS1_PH_UPDOWN: if (r > 1) { int per = 2 * r - 2, p = (int)(n % (uint32_t)per); d = p < r ? p : per - p; } break;
    case TS1_PH_RANDOM: d = (int)(mix(n, 77u, (uint32_t)v[TS1_P_SEED], 3u) % (uint32_t)r); break;
    case TS1_PH_WALK:                                     /* a step up or down from the last, folded into the range */
        for (k = 1, d = 0; k <= (int)idx; k++) {
            h = mix(cycle, (uint32_t)k, (uint32_t)v[TS1_P_SEED], 5u);
            d += (int)(h % 3u) - 1;
            if (d < 0) d = 1 < r ? 1 : 0;
            if (d >= r) d = r - 2 > 0 ? r - 2 : 0;
        }
        break;
    case TS1_PH_CHORD: d = (int)(n % (uint32_t)((r + 1) / 2)) * 2; break;
    default: break;
    }
    return v[TS1_P_DEGREE] + d;
}

static uint32_t put(ts1_event_t *out, uint32_t n, uint32_t max, uint32_t t, uint32_t len, int tr, int ch, int note, int vel)
{
    if (note < 0 || note > 127) return n;
    if (n < max) {
        ts1_event_t *e = &out[n];
        e->t = t; e->len = len ? len : 1u;
        e->track = (uint8_t)tr; e->ch = (uint8_t)ch; e->note = (uint8_t)note; e->vel = (uint8_t)clampi(vel, 1, 127);
    }
    return n + 1u;
}

uint32_t ts1_render(const ts1_song_t *s, uint32_t from, uint32_t to, ts1_event_t *out, uint32_t max)
{
    uint32_t n = 0, i, j;
    int t;
    if (to <= from) return 0;
    for (t = 0; t < (int)TS1_TRACKS; t++) {
        const int16_t *v = s->trk[t].v;
        uint32_t D = ts1_div_ticks[clampi(v[TS1_P_DIV], 0, TS1_NDIV - 1)], k, k0, k1;
        int sw = s->s[TS1_S_SWING] >= 50 && s->s[TS1_S_SWING] <= 75 ? s->s[TS1_S_SWING] - 50 : 0;   /* the master swing */
        int steps = v[TS1_P_STEPS], pulses = v[TS1_P_PULSES], late = clampi(v[TS1_P_GROOVE] - 50 + sw, 0, 25) * (int)D / 50;
        int jit = v[TS1_P_TEMP] * (int)D / 800, vj = v[TS1_P_TEMP] * 30 / 100;
        if (!v[TS1_P_ON] || pulses < 1) continue;
        k0 = from / D; k0 = k0 > 1u ? k0 - 1u : 0u;       /* a step a little before: late notes land in the span */
        k1 = to / D + 1u;
        for (k = k0; k <= k1; k++) {
            uint32_t cycle = k / (uint32_t)steps, step = k % (uint32_t)steps, idx = 0, np, r, nrep, G = 0;
            uint32_t g[8], h;
            int32_t t0;
            int vel, accent, deg = 0, v2, oct = 0;
            if (!ts1_pulse(steps, pulses, v[TS1_P_ROTATE], (int)step)) continue;
            for (i = 0; i < step; i++) idx += (uint32_t)ts1_pulse(steps, pulses, v[TS1_P_ROTATE], (int)i);
            np = cycle * (uint32_t)pulses + idx;         /* this pulse's number since the start */
            h = mix((uint32_t)k, (uint32_t)t, (uint32_t)v[TS1_P_SEED], 1u);
            if ((int)(h % 100u) >= v[TS1_P_PROB]) continue;
            accent = v[TS1_P_ACCENT] && ts1_pulse(pulses, v[TS1_P_ACCENT], 0, (int)idx);
            vel = v[TS1_P_VEL] + (accent ? v[TS1_P_ACCVEL] : 0);
            if (vj) vel += (int)(mix((uint32_t)k, (uint32_t)t, (uint32_t)v[TS1_P_SEED], 2u) % (uint32_t)(2 * vj + 1)) - vj;
            t0 = (int32_t)(k * D) + ((k & 1u) ? late : 0);
            if (jit) t0 += (int32_t)(mix((uint32_t)k, (uint32_t)t, (uint32_t)v[TS1_P_SEED], 4u) % (uint32_t)(2 * jit + 1)) - jit;
            if (t0 < 0) t0 = 0;
            if (v[TS1_P_KIND]) {
                if (v[TS1_P_MLEN] > 0) {                  /* a melody of its own length, over the rhythm's pulses */
                    uint32_t m = np % (uint32_t)v[TS1_P_MLEN];
                    deg = phrase_degree(v, m, 0u, m);
                } else
                    deg = phrase_degree(v, np, cycle, idx);
                if (v[TS1_P_FOLLOW]) deg += ts1_chord_degree(s, k * D);
                if (accent) {
                    if (v[TS1_P_ACCP] == 1) oct = 12;
                    else if (v[TS1_P_ACCP] == 2) deg += 4;
                    else if (v[TS1_P_ACCP] == 3) oct = -12;
                }
                if (v[TS1_P_TEMP] && (int)(mix((uint32_t)k, (uint32_t)t, (uint32_t)v[TS1_P_SEED], 6u) % 300u) < v[TS1_P_TEMP])
                    deg += (mix((uint32_t)k, (uint32_t)t, (uint32_t)v[TS1_P_SEED], 7u) & 1u) ? 1 : -1;
            }
            /* the repeats: gaps even, shrinking (speeding up) or growing */
            nrep = (uint32_t)v[TS1_P_REPEATS];
            for (r = 0; r < nrep; r++) {
                int a = v[TS1_P_PACE] < 0 ? -v[TS1_P_PACE] : v[TS1_P_PACE];
                g[r] = 8u + (uint32_t)a * 2u * (v[TS1_P_PACE] < 0 ? nrep - 1u - r : r);
                G += g[r];
            }
            for (r = 0, h = 0; r < nrep; r++) {
                uint32_t at = (uint32_t)t0 + D * h / G, len = D * (uint32_t)v[TS1_P_SUSTAIN] / 100u, gap = D * g[r] / G;
                h += g[r];
                if (nrep > 1u && len > gap) len = gap;
                if (at < from || at >= to) continue;
                if (!v[TS1_P_KIND]) { n = put(out, n, max, at, len, t, v[TS1_P_CH], v[TS1_P_NOTE], vel); continue; }
                for (v2 = 0; v2 < v[TS1_P_VOICES]; v2++) {
                    int dd = deg + 2 * v2, note = v[TS1_P_NOTE] + oct + s->s[TS1_S_ROOT] + ts1_scale_note(s->s[TS1_S_SCALE], dd);
                    if (v[TS1_P_SPREAD] && v2 == 1) note += 12;
                    n = put(out, n, max, at, len, t, v[TS1_P_CH], note, vel - v2 * 6);
                }
            }
        }
    }
    /* in time order (insertion: a span holds a few hundred notes) */
    if (n > max) n = max;
    for (i = 1; i < n; i++) {
        ts1_event_t e = out[i];
        for (j = i; j > 0 && (out[j - 1].t > e.t || (out[j - 1].t == e.t && out[j - 1].track > e.track)); j--) out[j] = out[j - 1];
        out[j] = e;
    }
    return n;
}
