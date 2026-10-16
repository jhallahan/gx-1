/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4: projects to measure the device with (SysEx 02 in a development build; the simulator's "bench").
 * Part of main_fm1.c (SEQ4_BENCH) and of the host simulator. */
/* the projects to measure (SysEx 02) */
void seq4_bench(int k)
{
    int s, i, t;
    s4_play(0);
    s4_panic();
    s4_lock(1);
    if (k == 1) {
        project_demo();
    } else if (k == 2) {                            /* the timing line: sixteenths, port A channel 1 */
        s4_defaults_glob();
        s4g.tempo = 1200;
        s4g.clock = 0;
        s4g.clk_out = 0;
        s4g.met_vol = 0;
        for (s = 0; s < S4_NSEQ; s++)
            s4_defaults_seq(s, M_MONO);
        for (i = 0; i < 16; i++) {
            s4[0].d.mono[i].vel = 100;
            s4[0].d.mono[i].gate = 50;
            s4[0].d.mono[i].cond = 100;
            s4[0].d.mono[i].deg = (int8_t)(i & 7);
        }
        s4[0].end = 15;
    } else {                                        /* everything at once, as busy as it gets */
        s4_defaults_glob();
        s4g.tempo = 2000;
        s4g.clock = 0;
        s4g.clk_out = 15;
        s4_defaults_seq(0, M_MULTI);
        for (t = 0; t < S4_NTRK; t++) {
            s4_trk_t *x = &s4[0].d.multi.t[t];
            x->port = (uint8_t)(t & 3);
            x->ch = (uint8_t)t;
            x->end = 127;
            for (i = 0; i < S4_NSTEP; i++) {
                s4_step_t *st = &s4[0].d.multi.s[t][i];
                st->vel = (uint8_t)(64 + (i * 7 + t * 13) % 63);
                st->gate = 40;
                st->cond = 100;
                st->rtg = (uint8_t)(i % 4 == 3 ? 4 : 0);
            }
        }
        for (i = 0; i < S4_NMOD; i++) {
            s4[0].ml[i].on = 1;
            s4[0].ml[i].xtype = X_CC;
            s4[0].ml[i].xnum = (uint8_t)(20 + i);
            s4[0].ml[i].mx = 127;
        }
        for (i = 0; i < S4_NSTEP; i++)
            for (t = 0; t < S4_NMOD; t++)
                s4[0].mod[t][i] = (uint8_t)((i * 9 + t * 31) & 127);
        s4_defaults_seq(1, M_POLY);
        s4[1].port = 1;
        s4[1].poly_max = S4_POLY;
        s4[1].end = 127;
        s4[1].d.poly.n = 0;
        for (i = 0; i < S4_NSTEP; i++)
            for (t = 0; t < S4_POLY; t++) {
                s4_pnote_t pn;
                pn.step = (uint8_t)i;
                pn.deg = (int8_t)(t * 2 + (i & 3));
                pn.vel = 90;
                pn.gate = 60;
                pn.oct = 0;
                pn.cond = 100;
                pn.rtg = 0;
                pn.ofs = 0;
                s4_poly_add(1, &pn);
            }
        s4_defaults_seq(2, M_CHORD);
        s4[2].port = 2;
        s4[2].end = 127;
        for (i = 0; i < S4_NSTEP; i++) {
            s4_cstep_t *c = &s4[2].d.chord[i];
            c->deg = (int8_t)((i / 4) % 7);
            c->vel = 100;
            c->gate = 80;
            c->cond = 100;
            c->type = (uint8_t)(i % 3);
            c->spread = 7;
            c->strum = 40;
        }
        s4[2].arp.mode = A_UPDN;
        s4[2].arp.div = DV_32;
        s4_defaults_seq(3, M_MONO);
        s4[3].port = 3;
        s4[3].div = DV_32;
        s4[3].end = 127;
        for (i = 0; i < S4_NSTEP; i++) {
            s4_step_t *st = &s4[3].d.mono[i];
            st->deg = (int8_t)(i % 11);
            st->vel = 100;
            st->gate = (uint8_t)(i % 5 == 0 ? S4_TIE : 50);
            st->cond = 100;
            st->rtg = (uint8_t)(i % 8 == 0 ? 2 : 0);
            st->glide = (uint8_t)(i % 3 ? 0 : 40);
        }
        for (i = 0; i < S4_NLFO; i++) {
            s4[3].lfo[i].wave = W_SINE;
            s4[3].lfo[i].rate = 59;
            s4[3].lfo[i].amt = 100;
            s4[3].lfo[i].xtype = X_CC;
            s4[3].lfo[i].xnum = (uint8_t)(70 + i);
            s4[3].lfo[i].xamt = 100;
        }
    }
    for (s = 0; s < S4_NSEQ; s++)
        s4_load_now(s);
    s4_lock(0);
    project_dirty_proj();
}
