/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4's engine: the clock, the four sequencers, what they send. One compilation unit (the parts are
 * included at the end), compiled at -O2 for the FM-1 and as is on the host.
 *
 * Time. The song position counts units, S4_U (3840) to a quarter note, which every division divides.
 * s4_tick (every ~0.7 ms on the FM-1) moves it: by the tempo, or following the MIDI clock coming in (each
 * pulse a 24th of a quarter, the time between them measured, never running ahead of the last pulse).
 * Each sequencer keeps its own position (its time bend and nudge on top). Each of its tracks (one, eight in
 * Multitrack; Matriceal's lanes) has a clock: the step playing, when the next begins. Half a step early the
 * next step is decided and its notes put in the sequencer's queue at their times (offsets, swing, repeats,
 * strums); the queue fires as the position passes them.
 *
 * Notes out. Each note scheduled gets a token; the note's off only acts if the note still has it (a newer
 * note on the same key took over), so a sequence can never cut another's note or leave one hanging. Stop
 * and panic turn off everything sounding.
 *
 * The UI changes steps and settings directly (a step half-read plays once as it was); everything else it
 * asks for through the command queue, done at the start of the next tick. */
#include <stddef.h>
#include "seq4.h"
#include "fastmath.h"

#ifdef EN_HOST
#define S4_POOL
#else
#define S4_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

s4_seq_t s4[S4_NSEQ] S4_POOL;
/* pattern_swap and snapshot_restore copy sequencers by words: s4[1] and s4[3] must be word aligned too */
_Static_assert(sizeof(s4_seq_t) % 4u == 0 && _Alignof(s4_seq_t) >= 4, "s4_seq_t must be word aligned");
s4_seq_t s4_stage[S4_NSEQ] S4_POOL;
uint8_t s4_dm_note[16] = { 36, 35, 38, 37, 39, 42, 46, 51, 75, 70, 60, 61, 62, 63, 64, 65 };   /* GX-1 drum map */
uint8_t s4_dm_ch[16] = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
s4_seq_t s4_snapbuf[S4_NSEQ] S4_POOL;
s4_glob_t s4g;
s4_song_t s4song[S4_NSONG] S4_POOL;
volatile s4_play_t s4p;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------------------------ random --- */
static uint32_t rng = 0x2545F491u;
uint32_t s4_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int rnd_n(int n) { return n > 0 ? (int)(s4_rand() % (uint32_t)n) : 0; }
static int chance(int pct) { return pct >= 100 || (pct > 0 && rnd_n(100) < pct); }

/* -------------------------------------------------------------------------------- divisions --- */
static const uint16_t DIV_U[DV_N] = {0, 0, 15360, 7680, 3840, 2560, 1920, 1280, 960, 640, 480, 320, 240};
static const char *const DIV_NAME[DV_N] = {"4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/6", "1/8", "1/12",
                                           "1/16", "1/24", "1/32", "1/48", "1/64"};
uint32_t s4_div_units(int dv)
{
    dv = clampi(dv, 0, DV_N - 1);
    return dv == DV_4BAR ? 4u * 15360u : dv == DV_2BAR ? 2u * 15360u : DIV_U[dv];
}
const char *s4_div_name(int dv) { return DIV_NAME[clampi(dv, 0, DV_N - 1)]; }

/* --------------------------------------------------------------------------------- commands --- */
enum { C_PLAY, C_STOPRESET, C_FILL, C_REC, C_LIVE, C_LIVECC, C_PANIC, C_LOADNOW, C_QPAT, C_MUTE, C_TAP,
       C_RESTART, C_SNAP, C_SONG, C_ROLL, C_CAPTURE };
typedef struct {
    uint8_t c, a, b, d;
    uint32_t x;
} cmd_t;
#define NCMD 64u
static cmd_t cq[NCMD];
static volatile uint32_t cq_w, cq_r;
static void cmd(int c, int a, int b, int d, uint32_t x)
{
    if (cq_w - cq_r >= NCMD)
        return;
    cq[cq_w % NCMD].c = (uint8_t)c;
    cq[cq_w % NCMD].a = (uint8_t)a;
    cq[cq_w % NCMD].b = (uint8_t)b;
    cq[cq_w % NCMD].d = (uint8_t)d;
    cq[cq_w % NCMD].x = x;
    BARRIER();
    cq_w++;
}
void s4_play(int on) { cmd(C_PLAY, on, 0, 0, 0); }
void s4_stop_reset(void) { cmd(C_STOPRESET, 0, 0, 0, 0); }
void s4_fill(int on) { cmd(C_FILL, on, 0, 0, 0); }
void s4_rec(int on) { cmd(C_REC, on, 0, 0, 0); }
void s4_live(int s, int trk, int note, int vel) { cmd(C_LIVE, s, trk, note, (uint32_t)vel); }
void s4_live_cc(int s, int trk, uint32_t st, uint32_t d1, uint32_t d2) { cmd(C_LIVECC, s, trk, 0, st | d1 << 8 | d2 << 16); }
void s4_panic(void) { cmd(C_PANIC, 0, 0, 0, 0); }
void s4_load_now(int s) { cmd(C_LOADNOW, s, 0, 0, 0); }
void s4_queue_pattern(int s, int pat) { cmd(C_QPAT, s, pat, 0, 0); }
void s4_queue_pattern_bar(int s, int pat) { cmd(C_QPAT, s, pat, 1, 0); }
void s4_mute(int s, int on) { cmd(C_MUTE, s, on, 0, 0); }
void s4_tap(void) { cmd(C_TAP, 0, 0, 0, 0); }
void s4_restart(int s) { cmd(C_RESTART, s < 0 ? 0xFF : s, 0, 0, 0); }
void s4_snapshot(int take) { cmd(C_SNAP, take + 1, 0, 0, 0); }
void s4_song(int on) { cmd(C_SONG, on, 0, 0, 0); }
void s4_roll(int s, int dv) { cmd(C_ROLL, s, dv, 0, 0); }
void s4_capture(int s) { cmd(C_CAPTURE, s, 0, 0, 0); }

/* ------------------------------------------------------------------------------- notes out --- */
static uint8_t ntok[S4_NPORT][16][128] S4_POOL;    /* the token of the note sounding (0 none, 1 live) */
static uint8_t tokc = 2;
static uint8_t tok_new(void)
{
    if (++tokc < 2)
        tokc = 2;
    return tokc;
}
static void out3(int port, int st, int d1, int d2)
{
    s4p.events++;
    s4_out((uint32_t)port & 3u, (uint32_t)st, (uint32_t)d1 & 0x7Fu, (uint32_t)d2 & 0x7Fu);
}
static void n_on(int port, int ch, int note, int vel, uint8_t tok)
{
    uint8_t *t = &ntok[port & 3][ch & 15][note & 127];
    if (*t)                                    /* sounding: again, from the top */
        out3(port, 0x80 | (ch & 15), note, 0);
    *t = tok;
    out3(port, 0x90 | (ch & 15), note, vel < 1 ? 1 : vel > 127 ? 127 : vel);
}
static void n_off(int port, int ch, int note, uint8_t tok)
{
    uint8_t *t = &ntok[port & 3][ch & 15][note & 127];
    if (*t && *t == tok) {
        *t = 0;
        out3(port, 0x80 | (ch & 15), note, 0);
    }
}
static void all_off(void)
{
    int p, c, n;
    for (p = 0; p < S4_NPORT; p++)
        for (c = 0; c < 16; c++)
            for (n = 0; n < 128; n++)
                if (ntok[p][c][n]) {
                    ntok[p][c][n] = 0;
                    out3(p, 0x80 | c, n, 0);
                }
}

/* ----------------------------------------------------------------------- the sequencers' state --- */
typedef struct {
    uint32_t t, len;           /* the step playing: its start, its length */
    uint32_t nt, nlen;         /* the next: when, how long (decided when prepared) */
    uint8_t pos, npos, prep, started;
    int8_t dirv;               /* pendulum's way */
    uint8_t prev;              /* the last step played a note (PRE) */
    uint8_t pend_rep;          /* pendulum: an end step played once already */
    uint8_t pulse;             /* Matriceal: pulses left on this step */
    uint16_t loops;            /* times round (A:B, FIRST) */
    uint16_t count;            /* steps since the start (swing's parity, Matriceal's reset) */
} clk_t;

typedef struct {               /* an event at a time */
    uint32_t t;
    uint8_t kind, port, ch, d1, d2, tok, trk, rsv;
} ev_t;
enum { E_ON, E_OFF, E_CC, E_PB, E_AT, E_PC };
#define NEV 224

typedef struct {               /* notes held into the next step (a tie, a legato) */
    uint8_t n, port, ch, leg;
    uint8_t note[8], tok[8];
} held_t;

typedef struct {               /* an arpeggiator's state */
    uint8_t in[16], inv[16], nin;      /* the notes it plays from (and their velocities), in order */
    uint8_t order[16];         /* the random-once order */
    uint8_t idx, oct, up, pulse, rptl, ostate;
    uint16_t eu;               /* the Euclidean step */
    uint32_t next;             /* when its next note is due (local units) */
    uint8_t port, ch, trk, latched;
    uint32_t until;            /* Poly / Chord: the step's notes feed it until then */
} arp_t;

typedef struct {
    uint32_t lpos;             /* the sequencer's own position */
    uint32_t bend_acc;
    uint8_t active;            /* running (not stopped by a toggling mute) */
    uint8_t waiting;           /* to start at the next launch boundary */
    clk_t c[S4_NTRK];
    clk_t lc[S4_NMX][S4_NLANE];
    clk_t mc[S4_NMOD];
    ev_t q[NEV];
    uint16_t nq;
    held_t h[S4_NTRK];
    arp_t karp, sarp;
    float lph[S4_NLFO], lval[S4_NLFO], lsh[S4_NLFO], lsh2[S4_NLFO];
    uint8_t lcc[S4_NLFO], mlast[S4_NMOD];
    float msm[S4_NMOD];
    uint32_t lsent[S4_NLFO];
    int16_t acc[S4_NMX];       /* Matriceal's interval sums */
    uint16_t mxtrig[S4_NMX];
    uint8_t prev[8], pn;       /* the last chord (auto voicing) */
    uint8_t pert[3];           /* the drum generator's chaos this bar */
    int8_t tpose;              /* transposed by notes coming in (semitones) */
    uint8_t roll;              /* the keyboard's roller: a division (0 off) */
    uint32_t roll_next;
    uint8_t live[S4_NTRK][16], nlive[S4_NTRK];   /* keys held (the arp's, the roller's) */
    uint8_t livev[S4_NTRK][16];
    uint8_t glide_on;
    uint32_t bars0;            /* where its reset counts from */
} rt_t;
static rt_t rt[S4_NSEQ] S4_POOL;

/* the modulation each destination gets now (LFOs, mod lanes, mappings), a sequencer's, a track's */
static int16_t dm[S4_NSEQ][S4_NTRK][S4_D_N];
static int16_t dmap[S4_NSEQ][S4_D_N];          /* the mappings' part */

static uint32_t gpos;          /* the song position */
static uint32_t gacc;          /* the tempo's remainder */
static uint8_t playing, paused;
static uint32_t now_us;        /* time, for tap tempo and the external clock */
static uint32_t ext_last_us, ext_iv;
static uint32_t ext_target;    /* how far the pulses received allow */
static uint8_t ext_armed;      /* a start came: the first pulse starts us */
static uint8_t ext_src;        /* where the clock comes from: cable */
static uint32_t ext_frac;
static uint32_t clk_next;      /* the next clock pulse out */
static uint32_t tap_t[8];
static uint8_t tap_n;
static uint8_t s4_sel;         /* the sequencer the UI shows (for MIDI in when not filtered) */
static uint8_t fill_on;
static uint8_t counting;
static uint32_t count_left;
static uint8_t snap_release;   /* a snapshot goes back at the next bar */
static uint8_t qpat[S4_NSEQ];  /* a staged pattern waits for its boundary (pattern + 1) */
static uint8_t qbar[S4_NSEQ];  /* GX-1: ... the next bar line instead (a new idea: in from its start) */

void s4_select(int s) { s4_sel = (uint8_t)(s & 3); }

/* ------------------------------------------------------------------------------ the queues --- */
static void q_add(int s, uint32_t t, int kind, int port, int ch, int d1, int d2, uint8_t tok, int trk)
{
    rt_t *r = &rt[s];
    int i;
    if (r->nq >= NEV) {
        s4p.drops++;
        if (kind == E_ON)                      /* full: notes don't start (their offs would go with them) */
            return;
        /* an off must go: fire the earliest instead to make room */
        {
            ev_t *e = &r->q[0];
            if (e->kind == E_OFF)
                n_off(e->port, e->ch, e->d1, e->tok);
            for (i = 1; i < r->nq; i++)
                r->q[i - 1] = r->q[i];
            r->nq--;
        }
    }
    if ((int32_t)(t - r->lpos) < 0)
        t = r->lpos;
    for (i = r->nq; i > 0 && (int32_t)(r->q[i - 1].t - t) > 0; i--)
        r->q[i] = r->q[i - 1];
    r->q[i].t = t;
    r->q[i].kind = (uint8_t)kind;
    r->q[i].port = (uint8_t)port;
    r->q[i].ch = (uint8_t)ch;
    r->q[i].d1 = (uint8_t)d1;
    r->q[i].d2 = (uint8_t)d2;
    r->q[i].tok = tok;
    r->q[i].trk = (uint8_t)trk;
    r->nq++;
}
static void q_fire(int s)
{
    rt_t *r = &rt[s];
    int k = 0, i;
    while (k < r->nq && (int32_t)(r->q[k].t - r->lpos) <= 0) {
        ev_t *e = &r->q[k];
        switch (e->kind) {
        case E_ON: n_on(e->port, e->ch, e->d1, e->d2, e->tok); break;
        case E_OFF: n_off(e->port, e->ch, e->d1, e->tok); break;
        case E_CC: out3(e->port, 0xB0 | e->ch, e->d1, e->d2); break;
        case E_PB: out3(e->port, 0xE0 | e->ch, e->d1, e->d2); break;
        case E_AT: out3(e->port, 0xD0 | e->ch, e->d1, 0); break;
        default: out3(e->port, 0xC0 | e->ch, e->d1, 0); break;
        }
        k++;
    }
    if (k) {
        for (i = k; i < r->nq; i++)
            r->q[i - k] = r->q[i];
        r->nq = (uint16_t)(r->nq - k);
    }
}
static void q_flush(int s)                     /* stop: the offs go now, the rest never */
{
    rt_t *r = &rt[s];
    int i;
    for (i = 0; i < r->nq; i++)
        if (r->q[i].kind == E_OFF)
            n_off(r->q[i].port, r->q[i].ch, r->q[i].d1, r->q[i].tok);
    r->nq = 0;
    for (i = 0; i < S4_NTRK; i++) {
        held_t *h = &r->h[i];
        int k;
        for (k = 0; k < h->n; k++)
            n_off(h->port, h->ch, h->note[k], h->tok[k]);
        h->n = 0;
    }
}

/* the parts' calls across one another */
static void seq_reset(int s, uint32_t at);
static void send_pc(int s);
static int at_boundary(uint32_t prev, uint32_t now, uint32_t bars);
static void mute_set(int s, int on);
static void tap(void);
static void rec_set(int on);
static void rec_cc(int ch, int kind, int d1, int d2);
static void rec_note(int s, int t, int note, int vel);
static void rec_commit(int s, int t, int pos);
static void lane_clock(int s, int l);
static void arp_feed(int s, const uint8_t *notes, const uint8_t *vels, int n, uint32_t T, uint32_t dur);
static void map_apply(int s, const s4_map_t *m, int v);
static void mx_step(int s, int t, clk_t *c, int pos, uint32_t T, uint32_t L);
static void live_note(int s, int trk, int note, int vel);
static void song_start(void);
static void song_tick(uint32_t prev);
static void snapshot_restore(void);
static void pattern_swap(int s);
static void transport_cc(int d1, int d2);

/* units from milliseconds at the tempo now */
static int32_t ms_units(int ms) { return (int32_t)((int64_t)ms * s4p.tempo * (int64_t)S4_U / 600000); }

#include "s4_music.c"
#include "s4_mod.c"
#include "s4_play.c"
#include "s4_arp.c"
#include "s4_rec.c"
#include "s4_edit.c"

/* --------------------------------------------------------------------------------- transport --- */
static void send_transport(int st)
{
    int p;
    for (p = 0; p < S4_NPORT; p++)
        if (s4g.clk_out >> p & 1u)
            s4_out((uint32_t)p, (uint32_t)st, 0, 0);
    if (s4g.tcc_ch && st != 0xF8)              /* the transport as CCs too: 105 play, 106 stop (port A) */
        out3(0, 0xB0 | ((s4g.tcc_ch - 1) & 15), st == 0xFC ? 106 : 105, 127);
}
static void send_pc(int s)                     /* a pattern's program change, bank select */
{
    s4_seq_t *q = &s4[s];
    if (q->mode == M_MULTI) {
        int t;
        for (t = 0; t < S4_NTRK; t++) {
            s4_trk_t *k = &q->d.multi.t[t];
            if (k->bank)
                out3(k->port, 0xB0 | k->ch, s4g.bank_lsb ? 32 : 0, k->bank - 1);
            if (k->pc)
                out3(k->port, 0xC0 | k->ch, k->pc - 1, 0);
        }
    }
    if (q->bank)
        out3(q->port, 0xB0 | q->ch, s4g.bank_lsb ? 32 : 0, q->bank - 1);
    if (q->pc)
        out3(q->port, 0xC0 | q->ch, q->pc - 1, 0);
}

static void seq_reset(int s, uint32_t at)      /* every clock from the top, at the sequencer's time at */
{
    rt_t *r = &rt[s];
    int t, l;
    for (t = 0; t < S4_NTRK; t++)
        clk_start(s, t, &r->c[t], at);
    for (t = 0; t < S4_NMX; t++) {
        for (l = 0; l < S4_NLANE; l++)
            clk_start_lane(s, t, l, &r->lc[t][l], at);
        r->acc[t] = 0;
        r->mxtrig[t] = 0;
    }
    for (l = 0; l < S4_NMOD; l++)
        clk_start_mod(s, l, &r->mc[l], at);
    r->karp.next = r->sarp.next = at;
    r->karp.eu = r->sarp.eu = 0;
    r->pn = 0;
    r->bars0 = gpos;
}

static void play_start(int from_top)
{
    int s;
    if (from_top) {
        gpos = 0;
        gacc = 0;
        clk_next = 0;
        s4p.bar = s4p.beat = 0;
        for (s = 0; s < S4_NSEQ; s++) {
            rt[s].lpos = 0;
            rt[s].bend_acc = 0;
            seq_reset(s, 0);
            rt[s].active = !s4[s].mute || s4g.mute_mode >= 2 ? 1 : rt[s].active;
            rt[s].waiting = 0;
        }
        send_transport(0xFA);
    } else {
        send_transport(0xFB);
    }
    playing = 1;
    paused = 0;
    s4p.on = 1;
    if (s4p.song_on)
        song_start();
}
static void play_stop(int pause)
{
    int s;
    if (!playing && !counting)
        return;
    playing = 0;
    counting = 0;
    s4p.count = 0;
    s4p.on = 0;
    paused = (uint8_t)pause;
    for (s = 0; s < S4_NSEQ; s++)
        q_flush(s);
    send_transport(0xFC);
    rec_stop();
    if (!pause)
        all_off();
}
static void transport_cc(int d1, int d2)       /* on the transport channel: CC 105 play, 106 stop, 107 record */
{
    if (d1 == 105 && d2 && !playing && !counting)
        play_start(!paused);
    else if (d1 == 106 && d2)
        play_stop(s4g.play_mode == 1);
    else if (d1 == 107)
        rec_set(d2 >= 64);
}

/* ---------------------------------------------------------------------------------- MIDI in --- */
static void ext_pulse(void)
{
    uint32_t iv = now_us - ext_last_us;
    ext_last_us = now_us;
    if (ext_armed) {                           /* the first pulse after a start: the downbeat */
        ext_armed = 0;
        play_start(1);
        ext_target = S4_U / 24u;
        ext_frac = 0;
        return;
    }
    if (iv > 2000u && iv < 200000u)            /* 12 .. 1250 BPM */
        ext_iv = ext_iv ? (ext_iv * 3u + iv) / 4u : iv;
    if (ext_iv)
        s4p.tempo = (uint16_t)clampi((int)(600000000u / (ext_iv * 24u)), 100, 9990);
    if (playing) {
        ext_target += S4_U / 24u;
        if ((int32_t)(ext_target - gpos) > (int32_t)(2u * S4_U / 24u))   /* far behind: catch up */
            gpos = ext_target - S4_U / 24u;
    }
}
static int clock_from(int cable)
{
    int src = s4g.clock;
    if (src == 0)
        return 0;
    if (src == 1)
        return cable < 4;
    if (src == 2)
        return cable == 14;
    return 1;
}

static void midi_note_in(int cable, int st, int note, int vel);
static void midi_cc_in(int cable, int st, int d1, int d2);

volatile uint32_t s4_gxq[S4_GXQ], s4_gxq_r, s4_gxq_w;
uint8_t s4_gx_ch;                                /* 0: off (SEQ-4 alone); GX-1 sets 16 */
void s4_midi_in(uint32_t pkt)
{
    int cable = (int)(pkt >> 4 & 15u), st = (int)(pkt >> 8 & 0xFFu), d1 = (int)(pkt >> 16 & 0x7Fu), d2 = (int)(pkt >> 24 & 0x7Fu);
    if (st >= 0xF8) {
        if (!clock_from(cable))
            return;
        if (st == 0xF8) {
            ext_src = (uint8_t)cable;
            s4p.ext = 1;
            ext_pulse();
        } else if (st == 0xFA && !s4g.start_ign) {
            if (playing)
                play_stop(0);
            ext_armed = 1;
            s4p.ext = 1;
        } else if (st == 0xFB && !s4g.start_ign) {
            ext_target = gpos;
            play_start(0);
        } else if (st == 0xFC && !s4g.start_ign) {
            play_stop(1);
            ext_armed = 0;
        }
        return;
    }
    s4p.in++;
    s4p.last_in[0] = (uint8_t)st;
    s4p.last_in[1] = (uint8_t)d1;
    s4p.last_in[2] = (uint8_t)d2;
    s4p.last_in_src = (uint8_t)cable;
    if (st == 0xF2) {                          /* song position: 16ths */
        if (!playing && clock_from(cable)) {
            gpos = (uint32_t)(d1 | d2 << 7) * (S4_U / 4u);
            ext_target = gpos;
        }
        return;
    }
    if (st < 0x80 || st >= 0xF0)
        return;
    if (s4_gx_ch && (st & 15) == s4_gx_ch - 1 && (st & 0xF0) != 0xA0 && (st & 0xF0) != 0xD0 && (st & 0xF0) != 0xE0 &&
        !((st & 0xF0) == 0xB0 && s4g.tcc_ch == s4_gx_ch && d1 >= 105 && d1 <= 107)) {   /* (the transport CCs stay SEQ-4's) */
        uint32_t w = s4_gxq_w;                   /* GX-1's channel: to the GEN pages (a full queue drops it) */
        if (w - s4_gxq_r < S4_GXQ) {
            s4_gxq[w % S4_GXQ] = (uint32_t)st | (uint32_t)d1 << 8 | (uint32_t)d2 << 16;
            s4_gxq_w = w + 1u;
        }
        return;
    }
    if (cable == 14 && s4g.thru)               /* the TRS jack's, on to a USB port */
        out3(s4g.thru - 1, st, d1, d2);
    else if (cable < 4 && s4g.usb_thru)
        out3(cable, st, d1, d2);
    if ((st & 0xF0) == 0x90 || (st & 0xF0) == 0x80)
        midi_note_in(cable, st, d1, (st & 0xF0) == 0x80 ? 0 : d2);
    else
        midi_cc_in(cable, st, d1, d2);
}

/* ---------------------------------------------------------------------------------- the tick --- */
static void do_cmd(const cmd_t *c)
{
    int s = c->a & 3;
    switch (c->c) {
    case C_PLAY:
        if (c->a) {
            if (!playing && !counting) {
                if (s4g.count_in && rec_armed()) {       /* the count-in first */
                    counting = 1;
                    count_left = count_units();
                    s4p.count = (uint8_t)(count_left / S4_U);
                    clk_next = 0;
                } else {
                    play_start(!paused);
                }
            }
        } else {
            play_stop(s4g.play_mode == 1);
        }
        break;
    case C_STOPRESET:
        play_stop(0);
        paused = 0;
        break;
    case C_FILL:
        fill_on = c->a;
        s4p.fill = c->a;
        break;
    case C_REC:
        rec_set(c->a);
        break;
    case C_LIVE:
        live_note(s, c->b, c->d, (int)c->x);
        break;
    case C_LIVECC: {
        int st = (int)(c->x & 0xFF), port, ch;
        trk_port(s, c->b, &port, &ch);
        out3(port, (st & 0xF0) | ch, (int)(c->x >> 8 & 0x7F), (int)(c->x >> 16 & 0x7F));
        break;
    }
    case C_PANIC:
        for (s = 0; s < S4_NSEQ; s++)
            q_flush(s);
        all_off();
        {
            int p, ch;
            for (p = 0; p < S4_NPORT; p++)
                for (ch = 0; ch < 16; ch++)
                    out3(p, 0xB0 | ch, 123, 0);
        }
        break;
    case C_LOADNOW:
        q_flush(s);
        seq_reset(s, rt[s].lpos);
        send_pc(s);
        break;
    case C_QPAT:
        qpat[s] = (uint8_t)(c->b + 1);
        qbar[s] = c->d;
        s4p.next[s] = c->b;
        if (!playing) {
            pattern_swap(s);
        }
        break;
    case C_MUTE:
        mute_set(s, c->b);
        break;
    case C_TAP:
        tap();
        break;
    case C_RESTART:
        for (s = 0; s < S4_NSEQ; s++)
            if (c->a == 0xFF || c->a == s) {
                q_flush(s);
                seq_reset(s, rt[s].lpos);
            }
        break;
    case C_SNAP:
        if (c->a == 2) {                     /* take */
            s4p.snap = 1;
            snap_release = 0;
        } else if (c->a == 1) {              /* release at the next bar */
            snap_release = 1;
        } else {
            s4p.snap = 0;
            snap_release = 0;
        }
        break;
    case C_SONG:
        s4p.song_on = c->a;
        if (c->a && playing)
            song_start();
        break;
    case C_ROLL:
        rt[s].roll = c->b;
        rt[s].roll_next = rt[s].lpos;
        break;
    case C_CAPTURE:
        capture_into(s);
        break;
    default:
        break;
    }
}

/* the boundaries a launch waits for: quantization 0 global, 1 none, 2.. 1,2,4,8 bars */
static uint32_t q_bars(int q)
{
    static const uint8_t B[] = {1, 1, 2, 4, 8};
    return q <= 1 ? 0 : B[clampi(q - 1, 0, 4)];
}
static int at_boundary(uint32_t prev, uint32_t now, uint32_t bars)
{
    uint32_t u = bars * 4u * S4_U;
    if (!bars)
        return 1;
    return prev / u != now / u;
}

static void boundaries(uint32_t prev)
{
    int s;
    uint32_t bar = 4u * S4_U;
    if (prev / bar != gpos / bar) {           /* a bar: snapshots back, songs, resets */
        s4p.bar++;
        if (snap_release) {
            snap_release = 0;
            s4p.snap = 0;
            snapshot_restore();
        }
        for (s = 0; s < S4_NSEQ; s++) {
            rt[s].pert[0] = (uint8_t)rnd_n(256);
            rt[s].pert[1] = (uint8_t)rnd_n(256);
            rt[s].pert[2] = (uint8_t)rnd_n(256);
            if (s4[s].reset && ((gpos - rt[s].bars0) / bar) >= s4[s].reset) {
                seq_reset(s, rt[s].lpos);
            }
        }
    }
    for (s = 0; s < S4_NSEQ; s++) {
        uint32_t bars = q_bars(s4[s].punch == 0 ? s4g.loadq : s4[s].punch);
        if (qpat[s] && at_boundary(prev, gpos, qbar[s] ? 1u : bars ? bars : q_bars(s4g.loadq))) {
            qbar[s] = 0;
            pattern_swap(s);
        }
        if (rt[s].waiting && at_boundary(prev, gpos, bars)) {
            rt[s].waiting = 0;
            rt[s].active = 1;
            seq_reset(s, rt[s].lpos);
        }
    }
    if (s4p.song_on)
        song_tick(prev);
}

static void metronome(uint32_t prev, uint32_t now)
{
    if (prev / S4_U != now / S4_U || now == 0) {
        int accent = (now / S4_U) % 4u == 0;
        if (s4g.met_mode == 2 || counting) {
            s4_click(accent);
            if (s4g.met_ch)
                out3(0, 0x90 | (s4g.met_ch - 1), accent ? 37 : 36, accent ? 120 : 90);
        }
    }
}

void s4_tick(uint32_t us)
{
    uint32_t prev, adv;
    int s;
    now_us += us;
    while (cq_r != cq_w) {
        cmd_t c = cq[cq_r % NCMD];
        BARRIER();
        cq_r++;
        do_cmd(&c);
    }
    if (!s4p.ext || !s4g.clock)
        s4p.tempo = s4g.tempo;
    if (s4p.ext && now_us - ext_last_us > 500000u) {   /* the clock stopped coming: our own */
        s4p.ext = 0;
        ext_armed = 0;
    }
    /* how far: the tempo's, or as far as the pulses allow */
    if (s4p.ext && s4g.clock) {
        uint32_t iv = ext_iv ? ext_iv : 20833u;
        ext_frac += us * (S4_U / 24u);
        adv = ext_frac / iv;
        ext_frac %= iv;
        if (playing && (int32_t)(gpos + adv - ext_target) > 0)
            adv = (int32_t)(ext_target - gpos) > 0 ? ext_target - gpos : 0;
    } else {
        gacc += (uint32_t)s4p.tempo * us * 4u;
        adv = gacc / 625000u;
        gacc %= 625000u;
    }
    if (counting) {                            /* the count-in: clicks, then play from the top */
        uint32_t c0 = count_units() - count_left;
        if (adv >= count_left) {
            counting = 0;
            s4p.count = 0;
            play_start(1);
            rec_count_done();
        } else {
            count_left -= adv;
            metronome(c0, c0 + adv);
            s4p.count = (uint8_t)((count_left + S4_U - 1) / S4_U);
        }
        lfo_tick(us, 0);
        return;
    }
    if (!playing) {
        for (s = 0; s < S4_NSEQ; s++) {        /* stopped: the keyboard's arps and the LFOs still run */
            rt[s].lpos += adv;
            arp_run(s);
            q_fire(s);
        }
        lfo_tick(us, 0);
        return;
    }
    prev = gpos;
    gpos += adv;
    s4p.pos = gpos;
    s4p.beat = gpos / S4_U;
    /* clock out */
    if (!s4p.ext || !s4g.clock)
        while ((int32_t)(gpos - clk_next) >= 0) {
            send_transport(0xF8);
            clk_next += S4_U / 24u;
        }
    metronome(prev, gpos);
    boundaries(prev);
    for (s = 0; s < S4_NSEQ; s++) {
        rt_t *r = &rt[s];
        int32_t b = s4[s].bend;
        if (b) {                               /* time bend: this sequencer faster or slower */
            r->bend_acc += adv * (uint32_t)(1000 + b * 10);
            r->lpos += r->bend_acc / 1000u;
            r->bend_acc %= 1000u;
        } else {
            r->lpos += adv;
        }
        if (r->active && !r->waiting)
            seq_run(s);
        arp_run(s);
        q_fire(s);
    }
    lfo_tick(us, 1);
}

void s4_init(void)
{
    int s;
    scales_init();
    for (s = 0; s < S4_NSEQ; s++) {
        rt[s].active = 1;
        s4p.active[s] = 1;
        s4p.next[s] = 0xFF;
        s4p.want[s] = 0xFF;
    }
    s4p.tempo = s4g.tempo ? s4g.tempo : 1200;
}

void s4_defaults_glob(void)
{
    s4_glob_t *g = &s4g;
    int i, k, s;
    for (i = 0; i < (int)sizeof *g; i++)
        ((uint8_t *)g)[i] = 0;
    g->magic = 0x47345153u;                    /* "SQ4G" */
    g->format = 1;
    g->tempo = 1200;
    g->clock = 3;                              /* follows a clock that comes, else its own */
    g->clk_out = 1;                            /* clock and start/stop on port A */
    g->loadq = 2;                              /* patterns change at the bar */
    g->thru = 1;                               /* the TRS jack's notes go on to port A */
    g->filt = 1;
    g->smooth = 0;
    g->mute_mode = 2;
    g->keep_mutes = 1;
    g->taps = 4;
    g->arp_reset = 1;
    g->harm_jump = 1;
    g->vdef = 100;
    g->gdef = 75;
    g->met_mode = 1;
    g->met_vol = 6;
    g->count_in = 3;
    g->rec_q = 1;
    g->rec_qpct = 100;
    g->rec_over = 1;
    g->auto_sync = 1;
    g->leds = 1;
    g->glow = 1;
    g->confirm_scale = 0;
    for (i = 0; i < S4_NSONG; i++)
        for (s = 0; s < S4_NSEQ; s++)
            for (k = 0; k < S4_NSLOT; k++) {
                s4song[i].slot[s][k].pat = S4_EMPTY;
                s4song[i].slot[s][k].rep = 1;
                s4song[i].slot[s][k].pc = 0;
                s4song[i].slot[s][k].bank = 0;
            }
}
