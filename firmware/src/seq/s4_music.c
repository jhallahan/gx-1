/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: scales, notes, chords. Part of seq4.c.
 *   scales    31 (the usual modes and the world's), each a 12-bit mask; a step's note is a degree of its
 *             sequencer's scale counted from the root, so changing the scale keeps the shape
 *   chords    a Chord step names a degree and a type; the types are built by stacking the scale's own
 *             thirds (triad, 7th, add 9, sus 2 and 4, 6th, 9th, 11th, 13th, power, octave). Bank A trades
 *             the harsh ones (diminished, augmented, dominant) for gentler neighbours; bank B keeps them
 *             and adds a few from outside the scale. Then the voicing (inversions, open), the spread (the
 *             chord's notes again an octave up), the bass (the root an octave down).
 *   names     note names (C3 = 60), chord names, and what chord a handful of notes is */

static const uint16_t SCALE_MASK[S4_NSCALE] = {
    0xFFF,                                   /* chromatic */
    0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,   /* major, minor, dorian, phrygian, lydian, mixolydian, locrian */
    0x295, 0x4A9,                            /* pentatonic major, minor */
    0x9AD, 0xAAD,                            /* harmonic minor, melodic minor */
    0x4E9,                                   /* blues */
    0x575,                                   /* arabian (major locrian) */
    0x6D3,                                   /* romanian (major) */
    0x18B,                                   /* balinese (pelog) */
    0x9CD, 0x6D9,                            /* hungarian minor, hungarian major */
    0x673,                                   /* oriental */
    0x9CB,                                   /* raga todi */
    0x8D1,                                   /* chinese */
    0x1A3, 0x18D,                            /* japanese (in), japanese (hirajoshi) */
    0x973,                                   /* persian */
    0xB6D,                                   /* diminished (whole-half) */
    0x5B3,                                   /* phrygian dominant */
    0x9B3,                                   /* byzantine (double harmonic) */
    0x57B,                                   /* spanish phrygian (8 tones) */
    0x9AB, 0xAAB,                            /* neapolitan minor, major */
    0x6CD,                                   /* ukrainian dorian */
};
static const char *const SCALE_NAME[S4_NSCALE + 1] = {
    "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Penta Maj",
    "Penta Min", "Harm Minor", "Mel Minor", "Blues", "Arabian", "Romanian", "Balinese", "Hungarian 1",
    "Hungarian 2", "Oriental", "Raga Todi", "Chinese", "Japanese 1", "Japanese 2", "Persian", "Diminished",
    "Phryg Dom", "Byzantine", "Spanish", "Neapol Min", "Neapol Maj", "Ukr Dorian", "Harmonizer"};

/* each scale's degrees as semitones, made at init */
static uint8_t scale_deg[S4_NSCALE][12], scale_n[S4_NSCALE];
static void scales_init(void)
{
    int sc, k;
    for (sc = 0; sc < S4_NSCALE; sc++) {
        scale_n[sc] = 0;
        for (k = 0; k < 12; k++)
            if (SCALE_MASK[sc] >> k & 1u)
                scale_deg[sc][scale_n[sc]++] = (uint8_t)k;
    }
}
int s4_scale_len(int sc) { return sc >= 0 && sc < S4_NSCALE ? scale_n[sc] : 12; }
const char *s4_scale_name(int sc) { return SCALE_NAME[(unsigned)sc <= S4_NSCALE ? sc : 0]; }

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int fmod_i(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

/* a degree in a scale (0 = chromatic for the harmonizer's sake): semitones from the root */
static int deg_semis(int sc, int deg)
{
    int n = s4_scale_len(sc >= S4_NSCALE ? 0 : sc);
    if (sc >= S4_NSCALE)
        sc = 0;
    return 12 * floordiv(deg, n) + scale_deg[sc][fmod_i(deg, n)];
}

static int seq_base(int s) { return 12 * (s4[s].oct + 2) + s4[s].root; }   /* C2 (oct 2) = 48 */

/* the harmonizer: a follower's degree played as the leader's chord tones */
static int harmonize(int s, int deg)
{
    int l = s4[s].leader % S4_NSEQ, n = s4p.chord_n[l], i, k, lo = 127, r, b, pc[12], np = 0;
    const volatile uint8_t *c = s4p.chord[l];
    if (!n || l == s)
        return seq_base(s) + deg_semis(0, deg);
    for (i = 0; i < n && i < 8; i++)
        if (c[i] < lo)
            lo = c[i];
    for (i = 0; i < n && i < 8; i++) {       /* the chord's pitch classes from its lowest, once each, in order */
        int p = fmod_i(c[i] - lo, 12), j;
        for (k = 0; k < np && pc[k] != p; k++)
            ;
        if (k < np)
            continue;
        for (j = np++; j > 0 && pc[j - 1] > p; j--)
            pc[j] = pc[j - 1];
        pc[j] = p;
    }
    b = seq_base(s);
    if (s4g.harm_base)                       /* the chord's root kept in the follower's base octave */
        r = b + fmod_i(lo - b, 12);
    else {
        r = lo + 12 * (s4[s].oct - s4[l].oct);
        if (s4g.harm_jump) {                 /* the chord, whole, no further than so many octaves from the base */
            int m = 12 * s4g.harm_jump;
            while (r > b + m)
                r -= 12;
            while (r < b - m)
                r += 12;
        }
    }
    return r + pc[fmod_i(deg, np)] + 12 * floordiv(deg, np);   /* (a row up is always a note up) */
}

int s4_note_of(int s, int deg, int oct)
{
    int n;
    if (s4[s].scale >= S4_HARMO)
        n = harmonize(s, deg) + 12 * oct;
    else
        n = seq_base(s) + deg_semis(s4[s].scale, deg) + 12 * oct;
    return n < 0 ? 0 : n > 127 ? 127 : n;
}
int s4_deg_of(int s, int note)
{
    int sc = s4[s].scale >= S4_HARMO ? 0 : s4[s].scale, n = s4_scale_len(sc), rel = note - seq_base(s);
    int oct = floordiv(rel, 12), pc = fmod_i(rel, 12), k, best = 0, bd = 99;
    for (k = 0; k < n; k++) {
        int d = pc - scale_deg[sc][k];
        if (d < 0)
            d = -d;
        if (d < bd || (d == bd && scale_deg[sc][k] > pc)) {   /* (ties: the upper, as quantizing does) */
            bd = d;
            best = k;
        }
    }
    if (pc > scale_deg[sc][n - 1] && 12 - pc < bd)          /* nearer the next octave's root */
        return (oct + 1) * n;
    return oct * n + best;
}

const char *s4_note_name(int note, char *b)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int o = note / 12 - 2, i = 0;
    const char *p = NM[note % 12];
    while (*p)
        b[i++] = *p++;
    if (o < 0) {
        b[i++] = '-';
        o = -o;
    }
    b[i++] = (char)('0' + o % 10);
    b[i] = 0;
    return b;
}

/* ------------------------------------------------------------------------------------ chords --- */
/* the types: a scale-built chord lists degrees above the root (stacked thirds and the like); a chromatic
 * one (bank B's additions, and the chromatic scale's) lists semitones */
typedef struct {
    const char *name;                        /* its suffix */
    uint8_t chroma;                          /* semitones, not degrees */
    uint8_t n;
    int8_t v[7];
} ctype_t;
static const ctype_t CT[] = {
    {"", 0, 3, {0, 2, 4}},                   /*  0 triad */
    {"7", 0, 4, {0, 2, 4, 6}},               /*  1 seventh */
    {"add9", 0, 4, {0, 2, 4, 8}},            /*  2 */
    {"sus2", 0, 3, {0, 1, 4}},               /*  3 */
    {"sus4", 0, 3, {0, 3, 4}},               /*  4 */
    {"6", 0, 4, {0, 2, 4, 5}},               /*  5 */
    {"9", 0, 5, {0, 2, 4, 6, 8}},            /*  6 */
    {"11", 0, 6, {0, 2, 4, 6, 8, 10}},       /*  7 */
    {"13", 0, 7, {0, 2, 4, 6, 8, 10, 12}},   /*  8 */
    {"5", 0, 2, {0, 4}},                     /*  9 power */
    {"oct", 0, 2, {0, 7}},                   /* 10 octave (in a 7-note scale) */
    /* chromatic shapes */
    {"", 1, 3, {0, 4, 7}},                   /* 11 major */
    {"m", 1, 3, {0, 3, 7}},                  /* 12 minor */
    {"dim", 1, 3, {0, 3, 6}},                /* 13 */
    {"aug", 1, 3, {0, 4, 8}},                /* 14 */
    {"7", 1, 4, {0, 4, 7, 10}},              /* 15 dominant */
    {"dim7", 1, 4, {0, 3, 6, 9}},            /* 16 */
    {"7sus4", 1, 4, {0, 5, 7, 10}},          /* 17 */
    {"maj7", 1, 4, {0, 4, 7, 11}},           /* 18 */
    {"m7", 1, 4, {0, 3, 7, 10}},             /* 19 */
    {"m7b5", 1, 4, {0, 3, 6, 10}},           /* 20 */
    {"6", 1, 4, {0, 4, 7, 9}},               /* 21 */
    {"m6", 1, 4, {0, 3, 7, 9}},              /* 22 */
    {"add9", 1, 4, {0, 4, 7, 14}},           /* 23 */
    {"madd9", 1, 4, {0, 3, 7, 14}},          /* 24 */
    {"sus2", 1, 3, {0, 2, 7}},               /* 25 */
    {"sus4", 1, 3, {0, 5, 7}},               /* 26 */
    {"5", 1, 2, {0, 7}},                     /* 27 */
    {"9", 1, 5, {0, 4, 7, 10, 14}},          /* 28 */
    {"maj9", 1, 5, {0, 4, 7, 11, 14}},       /* 29 */
    {"m9", 1, 5, {0, 3, 7, 10, 14}},         /* 30 */
    {"oct", 1, 2, {0, 12}},                  /* 31 */
};
/* the lists a degree offers, simplest first */
static const uint8_t BANK_A[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const uint8_t BANK_B[] = {0, 1, 6, 7, 8, 2, 3, 4, 5, 13, 14, 15, 16, 17, 20, 9, 10};
static const uint8_t CHROMA_A[] = {11, 12, 18, 19, 21, 22, 23, 24, 25, 26, 29, 30, 27, 31};
static const uint8_t CHROMA_B[] = {11, 12, 15, 18, 19, 13, 14, 16, 17, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 27, 31};

static int chromatic_seq(int s) { return s4[s].scale == 0 || s4[s].scale >= S4_HARMO; }
static const uint8_t *chord_list(int s, int *n)
{
    if (chromatic_seq(s)) {
        *n = s4[s].ch_bank ? (int)sizeof CHROMA_B : (int)sizeof CHROMA_A;
        return s4[s].ch_bank ? CHROMA_B : CHROMA_A;
    }
    *n = s4[s].ch_bank ? (int)sizeof BANK_B : (int)sizeof BANK_A;
    return s4[s].ch_bank ? BANK_B : BANK_A;
}
int s4_chord_types(int s, int deg)
{
    int n;
    (void)deg;
    chord_list(s, &n);
    return n;
}

/* the semitones of a type built on a degree; bank A softens the harsh ones */
static int chord_shape(int s, int deg, int type, int8_t *semi)
{
    int n, k, cnt, sc = s4[s].scale >= S4_HARMO ? 0 : s4[s].scale;
    const uint8_t *L = chord_list(s, &n);
    const ctype_t *t = &CT[L[(unsigned)type % (unsigned)n]];
    int r = deg_semis(sc, deg);
    cnt = t->n;
    for (k = 0; k < cnt; k++)
        semi[k] = (int8_t)(t->chroma ? t->v[k] : deg_semis(sc, deg + t->v[k]) - r);
    if (t == &CT[10] && s4_scale_len(sc) != 7)       /* an octave, whatever the scale's size */
        semi[1] = 12;
    if (!t->chroma && !s4[s].ch_bank && cnt >= 3) {  /* bank A: no diminished, augmented or dominant */
        int third = semi[1], fifth = semi[2];
        if (t->v[1] == 2 && t->v[2] == 4) {
            if (fifth == 6)                          /* diminished: the minor triad (a borrowed chord) */
                semi[2] = 7;
            else if (fifth == 8)                     /* augmented: the major */
                semi[2] = 7;
            if (cnt >= 4 && t->v[3] == 6 && third == 4 && semi[3] == 10)
                semi[3] = 9;                         /* dominant 7th: a 6th */
            if (cnt >= 4 && t->v[3] == 6 && third == 3 && fifth == 6)
                semi[3] = 10;                        /* half-diminished: minor 7th */
        }
    }
    return cnt;
}

static void sort_u8(uint8_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        uint8_t k = a[i];
        for (j = i - 1; j >= 0 && a[j] > k; j--)
            a[j + 1] = a[j];
        a[j + 1] = k;
    }
}

/* a chord step's notes (up to 8, sorted): the shape on its root, voiced, spread, with its bass */
static int chord_build(int s, int deg, int oct, int type, int voi, int spread, uint8_t *out)
{
    int8_t semi[8];
    int n = chord_shape(s, deg, type, semi), root, k, m;
    int tmp[16] = {0};
    if (s4[s].scale >= S4_HARMO)
        root = s4_note_of(s, deg, oct);
    else
        root = seq_base(s) + deg_semis(s4[s].scale, deg) + 12 * oct;
    for (k = 0; k < n; k++)
        tmp[k] = root + semi[k];
    switch (voi) {                            /* 1 open, 2 open 1st, 3 open 2nd, 4 1st inversion, 5 2nd */
    case 2:
    case 4:
        tmp[0] += 12;
        break;
    case 3:
    case 5:
        tmp[0] += 12;
        if (n > 1)
            tmp[1] += 12;
        break;
    default:
        break;
    }
    if ((voi == 1 || voi == 2 || voi == 3) && n >= 3) {   /* open: the middle note an octave up */
        int lo = 0, i;
        int sorted[16];
        for (i = 0; i < n; i++)
            sorted[i] = tmp[i];
        for (i = 1; i < n; i++) {
            int key = sorted[i], j = i - 1;
            while (j >= 0 && sorted[j] > key) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = key;
        }
        (void)lo;
        sorted[1] += 12;
        for (i = 0; i < n; i++)
            tmp[i] = sorted[i];
    }
    m = n;
    for (k = 0; k < spread && m < 8; k++)     /* the spread: the chord's notes again, an octave up */
        tmp[m++] = tmp[k % n] + 12 * (1 + k / n);
    if (s4[s].ch_bass) {                      /* the bass first: it has the 8th place before the spread */
        if (m >= 8)
            m = 7;
        tmp[m++] = root - 12;
    }
    for (k = 0; k < m; k++)
        out[k] = (uint8_t)(tmp[k] < 0 ? 0 : tmp[k] > 127 ? 127 : tmp[k]);
    sort_u8(out, m);
    return m;
}

int s4_chord_notes(int s, const s4_cstep_t *c, uint8_t *out)
{
    int types, t, v, sp;
    types = s4_chord_types(s, c->deg);
    t = ((int)c->type + s4[s].cg_type) % types;
    if (t < 0)
        t += types;
    v = ((int)c->voi + s4[s].cg_voi) % 6;
    if (v < 0)
        v += 6;
    sp = (int)c->spread + s4[s].cg_spread;
    sp = sp < 0 ? 0 : sp > 7 ? 7 : sp;
    return chord_build(s, c->deg, c->oct, t, v, sp, out);
}

/* the voicing closest to the last chord (the auto voicing): the same notes moved by octaves */
static int voice_lead(uint8_t *n, int cnt, const uint8_t *prev, int pn)
{
    int best = 0x7FFFFFFF, shift, bs = 0, k, j;
    if (!pn || cnt < 2)
        return cnt;
    for (shift = -2; shift <= 2; shift++) {   /* whole chord moved, and inversions by octave rotation */
        int inv;
        for (inv = 0; inv < cnt; inv++) {
            int cost = 0, t[8];
            for (k = 0; k < cnt; k++)
                t[k] = n[k] + 12 * shift + (k < inv ? 12 : 0);
            for (k = 0; k < cnt; k++) {
                int d = 999;
                for (j = 0; j < pn; j++) {
                    int e = t[k] - prev[j];
                    e = e < 0 ? -e : e;
                    if (e < d)
                        d = e;
                }
                cost += d;
            }
            if (cost < best) {
                best = cost;
                bs = shift * 16 + inv;
            }
        }
    }
    {
        int shift2 = floordiv(bs, 16), inv = fmod_i(bs, 16);
        for (k = 0; k < cnt; k++) {
            int v = n[k] + 12 * shift2 + (k < inv ? 12 : 0);
            n[k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        }
        sort_u8(n, cnt);
    }
    return cnt;
}

const char *s4_chord_name(int s, const s4_cstep_t *c, char *b)
{
    int8_t semi[8];
    int types = s4_chord_types(s, c->deg), t = ((int)c->type + s4[s].cg_type) % types, n, i = 0;
    const char *suf;
    char nb[6];
    const uint8_t *L;
    int ln;
    if (t < 0)
        t += types;
    n = chord_shape(s, c->deg, t, semi);
    L = chord_list(s, &ln);
    {
        int root = s4[s].scale >= S4_HARMO ? s4_note_of(s, c->deg, 0) : seq_base(s) + deg_semis(s4[s].scale, c->deg);
        const char *p = s4_note_name(root, nb);
        while (*p && !(*p >= '0' && *p <= '9') && *p != '-')
            b[i++] = *p++;
    }
    suf = CT[L[t]].name;
    if (!CT[L[t]].chroma && n >= 3) {          /* a scale-built chord: name it by what it turned out to be */
        int third = semi[1], fifth = semi[2];
        if (CT[L[t]].v[1] == 2 && CT[L[t]].v[2] == 4) {
            int sev = n >= 4 && CT[L[t]].v[3] == 6 ? semi[3] : -1;
            if (third == 3 && fifth == 6)
                suf = sev == 9 ? "dim7" : sev == 10 ? "m7b5" : "dim";
            else if (third == 4 && fifth == 8)
                suf = "aug";
            else if (third == 3)
                suf = sev == 10 ? (n >= 5 ? "m9" : "m7") : sev == 11 ? "mMaj7" : n == 4 && CT[L[t]].v[3] == 8 ? "madd9"
                      : n == 4 && CT[L[t]].v[3] == 5 ? "m6" : "m";
            else
                suf = sev == 11 ? (n >= 5 ? "maj9" : "maj7") : sev == 10 ? (n >= 5 ? "9" : "7") : sev == 9 ? "6" : CT[L[t]].name;
            if (n >= 6)
                suf = CT[L[t]].name;
        }
    }
    while (*suf && i < 10)
        b[i++] = *suf++;
    b[i] = 0;
    return b;
}

/* what chord some notes are: the root and a quality from the set of pitch classes */
const char *s4_detect_chord(const uint8_t *notes, int n, char *b)
{
    static const struct { uint16_t m; const char *s; } Q[] = {
        {0x091, ""}, {0x089, "m"}, {0x049, "dim"}, {0x111, "aug"}, {0x085, "sus2"}, {0x0A1, "sus4"},
        {0x891, "maj7"}, {0x491, "7"}, {0x489, "m7"}, {0x449, "m7b5"}, {0x249, "dim7"}, {0x291, "6"},
        {0x289, "m6"}, {0x895, "maj9"}, {0x495, "9"}, {0x48D, "m9"}, {0x095, "add9"}, {0x08D, "madd9"},
        {0x4A1, "7sus4"}, {0x889, "mMaj7"}, {0x081, "5"}};
    char nb[6];
    uint16_t pcs = 0;
    int k, r, i = 0, lo = 127;
    b[0] = 0;
    if (n <= 0)
        return b;
    for (k = 0; k < n; k++) {
        pcs |= (uint16_t)(1u << (notes[k] % 12));
        if (notes[k] < lo)
            lo = notes[k];
    }
    for (r = 0; r < 12; r++) {                /* the bass's root first, then the others */
        int root = (lo + r) % 12;
        uint16_t rot = (uint16_t)(((pcs >> root) | (pcs << (12 - root))) & 0xFFF);
        unsigned q;
        if (!(rot & 1u))
            continue;
        for (q = 0; q < sizeof Q / sizeof Q[0]; q++)
            if (rot == Q[q].m) {
                const char *p = s4_note_name(root + 60, nb), *s = Q[q].s;
                while (*p && !(*p >= '0' && *p <= '9'))
                    b[i++] = *p++;
                while (*s)
                    b[i++] = *s++;
                if (root != lo % 12) {        /* a slash chord: the bass */
                    b[i++] = '/';
                    p = s4_note_name(lo, nb);
                    while (*p && !(*p >= '0' && *p <= '9'))
                        b[i++] = *p++;
                }
                b[i] = 0;
                return b;
            }
    }
    if (n == 1) {                             /* a lone note: its name */
        s4_note_name(notes[0], b);
        return b;
    }
    return b;
}
