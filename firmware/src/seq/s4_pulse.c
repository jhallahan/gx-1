/* SPDX-License-Identifier: GPL-3.0-only */
/* Pulse mode: the TS-1 pulse sequencer (firmware/src/ts1, tools/sync_gx.sh) played on the sequencer's clock. Each
 * tick renders the notes that start in the span just played (TS-1 is a pure function of the song and the time),
 * sends them on port A, and ends each when its length is up. Part of seq4.c. */
#define clampi ts1_clampi_                       /* ts1.c has its own */
#include "../ts1/ts1_fm1.h"
#include "../ts1/ts1.c"
#undef clampi

ts1_song_t s4_ps;
volatile uint8_t s4_pulse_on;
volatile uint32_t s4_pulse_hit[TS1_TRACKS];
volatile uint32_t s4_pulse_pos;

#define PU (S4_U / TS1_PPQ)                      /* engine units a TS-1 tick (40) */
#define NPOFF 96u
typedef struct { uint32_t t; uint8_t ch, note, tok; } poff_t;
static poff_t poff[NPOFF];                       /* the notes sounding: when each ends */
static uint32_t npoff;
static ts1_event_t pev[64];

static void pulse_off_at(uint32_t i)
{
    n_off(0, poff[i].ch, poff[i].note, poff[i].tok);
    poff[i] = poff[--npoff];
}
static void pulse_run(uint32_t prev, uint32_t now)
{
    uint32_t i, n, from = prev / PU, to = now / PU;
    for (i = 0; i < npoff;)                      /* the notes whose time is up */
        if ((int32_t)(now - poff[i].t) >= 0)
            pulse_off_at(i);
        else
            i++;
    s4_pulse_pos = to;
    if (to <= from)
        return;
    n = ts1_render(&s4_ps, from, to, pev, sizeof pev / sizeof pev[0]);
    if (n > sizeof pev / sizeof pev[0])
        n = sizeof pev / sizeof pev[0];
    for (i = 0; i < n; i++) {
        const ts1_event_t *e = &pev[i];
        uint8_t tok = tok_new();
        uint32_t j, end = (e->t + e->len) * PU;
        for (j = 0; j < npoff; j++)              /* the same note again: the last one ends first */
            if (poff[j].ch == e->ch && poff[j].note == e->note) {
                pulse_off_at(j);
                break;
            }
        if (npoff >= NPOFF) {                    /* full: the one ending soonest goes now */
            uint32_t k, m = 0;
            for (k = 1; k < npoff; k++)
                if ((int32_t)(poff[k].t - poff[m].t) < 0)
                    m = k;
            pulse_off_at(m);
        }
        n_on(0, e->ch, e->note, e->vel, tok);
        poff[npoff].t = end > now ? end : now + 1u;
        poff[npoff].ch = e->ch;
        poff[npoff].note = e->note;
        poff[npoff].tok = tok;
        npoff++;
        if (e->track < TS1_TRACKS)
            s4_pulse_hit[e->track] = now;
    }
}
static void pulse_stop(void)                     /* everything Pulse was sounding, ended */
{
    while (npoff)
        pulse_off_at(npoff - 1u);
}
