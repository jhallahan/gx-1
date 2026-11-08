/* SPDX-License-Identifier: GPL-3.0-only
 * Boot, audio ISR and main loop adapted from Felucca's main.c / audio.c, Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* SEQ-4 on the FM-1 (from IND-1's, AMB-1's and X0X's): boot (WDT first, boot-loop guard, guards), LCD, input (TIMER5
 * 10 kHz), audio (ALNK0), USB; then the main loop: UI at ~60 frames/s, input polled in between. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* --------------------------------------------------------------- audio --- */
/* SEQ-4's sound is only the metronome; the audio interrupt is the sequencer's clock. Its halves are short
 * (32 frames, 0.73 ms): the engine runs once a half, so a note goes out within that of its time. */
#define HALF_FRAMES 32u                     /* I2S half buffer: 0.73 ms at 44.1 kHz */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FS 44100u
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x44424731u                       /* "DBG1": read with `fm1t memr` */
struct en_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots, stage;
    uint32_t crash_seen;                            /* the crash count already reported at a boot */
} en_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;                           /* two failed boots: no audio, USB on (safe_main) */
static uint32_t tick_max_us, midi_rx_count, gap_max_us;

/* the click: a decaying sine (a two-pole resonator), high on the bar's first beat */
static int32_t ck_y1, ck_y2, ck_c, ck_env, ck_gain;
void s4_click(int accent)
{
    if (!s4g.met_vol)
        return;
    ck_c = accent ? 31447 : 32023;                  /* 2 cos(w) Q14: 2 kHz, 1.5 kHz */
    ck_y2 = 0;
    ck_y1 = accent ? 9211 : 6950;                   /* 32768 sin(w) */
    ck_env = 32767;
    ck_gain = (int32_t)(s4g.met_vol * master_q12 / 10u);
}
static void click_render(int32_t *o, uint32_t n)
{
    uint32_t i;
    if (!ck_env) {
        for (i = 0; i < 2u * n; i++)
            o[i] = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t y = (ck_c * ck_y1 >> 14) - ck_y2, v;
        ck_y2 = ck_y1;
        ck_y1 = y;
        v = (y * ck_env >> 15) * ck_gain >> 12;
        ck_env = ck_env * 32653 >> 15;              /* ~25 ms */
        o[2u * i] = o[2u * i + 1u] = v << 7;
    }
    if (ck_env < 64)
        ck_env = 0;
}

static void render_block(int32_t *o)                /* ALNK0: the MIDI in, the engine, the click; measured */
{
    static uint32_t last, acc;
    uint32_t t0 = fm1_ticks(), us, dt, pkt, k = 0, budget = HALF_FRAMES * 1000000u / FS;
    en_dbg.in_audio = 1;
    acc += last ? t0 - last : budget * FM1_TICKS_PER_US;
    last = t0;
    dt = acc / FM1_TICKS_PER_US;                    /* the time since the last, to the microsecond */
    acc -= dt * FM1_TICKS_PER_US;
    if (dt > gap_max_us && en_dbg.halves > 100u)
        gap_max_us = dt;
    while (k < 64u && plat_midi_in(&pkt)) {
        s4_midi_in(pkt);
        midi_rx_count++;
        k++;
    }
    s4_tick(dt);
    click_render(o, HALF_FRAMES);
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    en_dbg.cpu_q8 = (en_dbg.cpu_q8 * 63u + (us * 256u) / budget) / 64u;
    audio_cpu_pct = (en_dbg.cpu_q8 * 100u) >> 8;
    en_dbg.halves++;
    en_dbg.last_us = us;
    if (us > en_dbg.max_us)
        en_dbg.max_us = us;
    if (us > tick_max_us)
        tick_max_us = us;
    en_dbg.in_audio = 0;
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
            en_dbg.late++;                         /* the DMA moved on while we rendered */
            audio_xruns++;
        }
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
    ck_env = 0;
}

/* SEQ-4's SysEx (F0 7D 53 34 cmd .. F7, 7D the non-commercial ID), each value as five 7-bit groups:
 *   01  the load: cpu %, the slowest tick (us), ticks late, ticks, a bench build, the slowest tick since 03,
 *       the core's MHz, screen frames, playing | external << 1 | recording << 2, MIDI out, MIDI dropped,
 *       MIDI in, the screen's time (us, summed) and its slowest frame, the tempo, the longest gap between
 *       ticks (us), the log's bytes live and its room, the USB ring's high water, USB up | suspended << 1,
 *       the boots counted (a crash restarts it)
 *   03  the slowest and the late counted again       04 s  the screen not drawn for s seconds
 *   05  the store: flash ok, the log's last save, each object's, the patterns saved (two 32-bit masks
 *       a pair of sequencers), live bytes, room
 *   07  what plays: each sequencer's step, pattern, mode, mute; the bar, the beat
 * A development build (./build.sh --bench, -DSEQ4_BENCH) also takes 02 k (a project to measure: 0 all four
 * as busy as they get, 1 the demo, 2 a plain sixteenth line on port A channel 1 to time), 06 p (play, stop),
 * 08 (the demo, saved), 0A (the screen's parts), 0C v s (a view, a sequencer: the screen to measure), 0D s p
 * (pattern p loaded on sequencer s) and 0E t (a snapshot taken, 1, or given back at the bar, 0).
 * GX-1 (every build): 10 g s0..s4 [p] a new idea (genre 1 deep_dnb, 2 breaks, 3 autonomic, 4 dub_techno, 5 idm, 6 electro, 7 uk_garage, 8 minimal, 9 downtempo; the seed as five
 * 7-bit groups; a part 0..5), 11 p a part, 12 s0..s4 compose (locked parts kept), 13 r on lock, 14 r m i mutate
 * (role, aspects mask, intensity), 15 undo, 16 redo, 17 m v a macro, 18 0|1 the map (Tonverk split, GM).
 * Each replies 10 with the events staged (or 0x7FFFFFFF if it failed). */
#ifdef SEQ4_BENCH
void seq4_bench(int k);
void ui_dev_view(int v, int s);
extern void (*ui_mark)(int part);
static uint32_t ui_part[10], ui_mark_t;           /* where the screen's frames go (SysEx 0A) */
static void dev_mark(int p)
{
    uint32_t t = fm1_ticks();
    if (p)
        ui_part[p] += t - ui_mark_t;
    ui_mark_t = t;
}
#endif
static void dev_put(uint8_t *b, uint32_t v)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        b[k] = (uint8_t)((v >> (7u * k)) & 0x7Fu);
}
static uint32_t ui_pause_until;                     /* SysEx 04: the screen not drawn, to see what it costs */
static uint32_t ui_us, ui_us_max;                   /* the screen's frames: their time, the slowest (SysEx 01) */
static void dev_reply(uint8_t *r, uint32_t cmd, uint32_t nvals)
{
    r[0] = 0xF0;
    r[1] = 0x7D;
    r[2] = 0x53;
    r[3] = 0x34;
    r[4] = (uint8_t)cmd;
    r[5 + 5u * nvals] = 0xF7;
    ota_frame_done();
    ota_wire_send(r, 6u + 5u * nvals);
}
static void dev_sysex(void)
{
    const uint8_t *p;
    uint32_t n;
    static uint8_t r[6 + 5 * 24 + 1];
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != 0x7Du || p[1] != 0x53u || p[2] != 0x34u)
        return;                                     /* not ours: left for the updater */
    switch (p[3]) {
    case 1: {
        uint32_t c0, t0, mhz, v[21], k;
        c0 = fm1_perf_cycles();                     /* the core's clock: its cycles over 200 us */
        t0 = fm1_ticks();
        while ((uint32_t)(fm1_ticks() - t0) < 200u * FM1_TICKS_PER_US)
            ;
        mhz = (fm1_perf_cycles() - c0) / 200u;
        v[0] = (en_dbg.cpu_q8 * 100u) >> 8;
        v[1] = en_dbg.max_us;
        v[2] = en_dbg.late;
        v[3] = en_dbg.halves;
#ifdef SEQ4_BENCH
        v[4] = 1u;
#else
        v[4] = 0u;
#endif
        v[5] = tick_max_us;
        v[6] = mhz;
        v[7] = en_dbg.ui_frames;
        v[8] = (uint32_t)s4p.on | (uint32_t)s4p.ext << 1 | (uint32_t)s4p.rec << 2;
        v[9] = s4p.events;
        v[10] = s4p.drops | midi_drops << 16;     /* the engine's queue full, the USB ring full */
        v[11] = midi_rx_count;
        v[12] = ui_us;
        v[13] = ui_us_max;
        v[14] = s4p.tempo;
        v[15] = gap_max_us;
        v[16] = plat_log_live();
        v[17] = plat_log_capacity();
        v[18] = midi_out_hw;
        v[19] = (uint32_t)usb.config | (uint32_t)usb.suspended << 1;
        v[20] = en_dbg.boots;                       /* (a crash and its restart: one more) */
        for (k = 0; k < 21u; k++)
            dev_put(r + 5 + 5 * k, v[k]);
        dev_reply(r, 1, 21);
        return;
    }
    case 3:
        en_dbg.max_us = 0;
        en_dbg.late = 0;
        tick_max_us = 0;
        gap_max_us = 0;
        ui_us_max = 0;
        midi_out_hw = 0;
        break;
    case 4:
        ui_pause_until = fm1_ms + 1000u * (n >= 5u ? p[4] : 5u);
        break;
    case 5: {
        uint32_t m[4] = {0, 0, 0, 0}, s, q;
        for (s = 0; s < S4_NSEQ; s++)
            for (q = 0; q < S4_NPAT; q++)
                if (plat_log_has(KEY_PAT(s, q)))
                    m[s / 2u] |= 1u << ((s & 1u) * 16u + q);
        dev_put(r + 5, flash_ok);
        dev_put(r + 10, (uint32_t)(log_rc + 100));
        dev_put(r + 15, (uint32_t)(store_rc[OBJ_GLOB] + 100));
        dev_put(r + 20, (uint32_t)(store_rc[OBJ_PANEL] + 100));
        dev_put(r + 25, m[0]);
        dev_put(r + 30, m[1]);
        dev_put(r + 35, plat_log_live());
        dev_put(r + 40, plat_log_capacity());
        dev_reply(r, 5, 8);
        return;
    }
    case 7: {
        uint32_t s;
        for (s = 0; s < S4_NSEQ; s++)
            dev_put(r + 5 + 5 * s, (uint32_t)s4p.step[s][0] | (uint32_t)s4p.pat[s] << 8 |
                                       (uint32_t)s4[s].mode << 16 | (uint32_t)s4[s].mute << 20);
        dev_put(r + 25, s4p.bar);
        dev_put(r + 30, s4p.beat);
        dev_reply(r, 7, 6);
        return;
    }
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: {
        uint32_t seed = n >= 9u ? (uint32_t)p[4 + (p[3] == 0x10)] : 0u, k;
        int res = -1;
        for (k = 0; k < 5u && 4u + (p[3] == 0x10) + k < n; k++)
            seed |= (uint32_t)p[4 + (p[3] == 0x10) + k] << (7u * k);
        ota_frame_done();
        switch (p[3]) {
        case 0x10: res = n >= 10u ? gxb_new(p[4], seed) : -1; if (res >= 0 && n >= 11u) res = gxb_part(p[10]); break;
        case 0x11: res = n >= 5u ? gxb_part(p[4]) : -1; break;
        case 0x12: res = n >= 9u ? gxb_compose(seed) : -1; break;
        case 0x13: res = n >= 6u ? gxb_lock(p[4] & 3, p[5]) : -1; break;
        case 0x14: res = n >= 7u ? gxb_mutate(p[4] & 3, p[5], p[6], s4_rand()) : -1; break;
        case 0x15: res = gxb_undo(); break;
        case 0x16: res = gxb_redo(); break;
        case 0x17: res = n >= 6u ? gxb_macro(p[4], p[5]) : -1; break;
        case 0x18: res = n >= 5u ? gxb_set_map(p[4] ? GX_MAP_GM : GX_MAP_TONVERK_SPLIT) : -1; break;
        }
        dev_put(r + 5, res < 0 ? 0x7FFFFFFFu : (uint32_t)res);
        dev_reply(r, 0x10, 1);
        return;
    }
#ifdef SEQ4_BENCH
    case 2:
        if (n >= 5u)
            seq4_bench(p[4]);
        break;
    case 6:                                         /* play (1) or stop (0), as PLAY */
        s4_play(n >= 5u ? p[4] : 1);
        break;
    case 8:                                         /* the demo, saved at once */
        s4_play(0);
        s4_panic();
        ota_frame_done();
        project_demo();
        project_save_all();
        return;
    case 10: {                                      /* the screen's parts: ticks (24 MHz) each, the transfers' wait */
        uint32_t k;
        ui_mark = dev_mark;
        for (k = 0; k < 10u; k++)
            dev_put(r + 5 + 5 * k, ui_part[k]);
        dev_put(r + 55, lcd_wait_t);
        dev_reply(r, 10, 11);
        return;
    }
    case 12:
        if (n >= 6u)
            ui_dev_view(p[4], p[5]);
        break;
    case 13:                                        /* a pattern loaded on a sequencer, as PRESETS does */
        if (n >= 6u) {
            ota_frame_done();
            project_load_pattern(p[4] & 3, p[5] & 15);
            return;
        }
        break;
    case 14:                                        /* a snapshot taken (1), or given back at the bar (0) */
        if (n >= 5u) {
            int s;
            if (p[4])
                for (s = 0; s < S4_NSEQ; s++)
                    s4_snapbuf[s] = s4[s];
            s4_snapshot(p[4] ? 1 : 0);
        }
        break;
#endif
    default: break;
    }
    ota_frame_done();
}

#ifdef SEQ4_BENCH
#include "bench.c"
#endif

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    en_dbg.timer_irqs++;
    if (en_dbg.in_audio)
        en_dbg.nested++;
    fm1_input_tick();
    /* 2 kHz: all USB SIE traffic lives here. This ISR now nests into the audio render (so the key
     * matrix keeps its rhythm, below); USB shares the MIDI queues with the audio ISR, so a poll that
     * falls inside the render waits for the first tick outside it, as it did before the nesting. */
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        /* the USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes
         * up to ~11.6 ms): it runs nested too. It touches only EP4 (INDEX is set on every access) and
         * the consumer side of its ring, and usb_poll never runs nested, so the two never interleave */
        if (en_dbg.in_audio)
            uac_service();
#endif
    }
    if (usb_due && !en_dbg.in_audio) {
        usb_due = 0;
        usb_poll();
#if EN_TRS
        uart_midi_poll();                           /* the TRS jack: same context, same queue */
#endif
    }
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in main.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "GX-1 CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* SAFE MODE: SEQ-4 crashed (or hung) twice while starting. Nothing that makes sound runs: no audio,
 * no engine, no UI; USB is on, so the web installer (SEQ-4, or the stock firmware) and M-UPGRADE can
 * reach it. PLAY tries SEQ-4 again. The chip's own update mode stays the last resort (fm1_cstart). */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_B, "GX-1 SAFE MODE", C_HI, 1);
    draw_text_box(0, 52, 240, &FONT_S, "IT CRASHED TWICE", C_WHITE, 1);
    draw_text_box(0, 72, 240, &FONT_S, "WHILE STARTING.", C_WHITE, 1);
    draw_text_box(0, 102, 240, &FONT_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    draw_text_box(0, 122, 240, &FONT_S, "REINSTALL FROM THE", C_WHITE, 1);
    draw_text_box(0, 142, 240, &FONT_S, "WEB INSTALLER.", C_WHITE, 1);
    draw_text_box(0, 172, 240, &FONT_S, "PLAY: TRY AGAIN", C_HI, 1);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 208, 240, &FONT_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    t0 = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try SEQ-4 again, from scratch */
            bootguard.pending = 0;
            draw_text_box(0, 172, 240, &FONT_S, "STARTING GX-1...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

static void splash(void)                            /* the name, and the four sequencers' colours */
{
    static const uint16_t C4[4] = {RGB(0, 190, 255), RGB(255, 70, 190), RGB(255, 170, 0), RGB(80, 230, 120)};
    int32_t i, j;
    lcd_fill(0, 0, 240, 240, RGB(12, 12, 16));
    cv_begin(240, 150, RGB(12, 12, 16));
    for (j = 0; j < 4; j++)                         /* a few pads lit, in each one's colour */
        for (i = 0; i < 16; i++) {
            int on = ((i * 5 + j * 3) % 7) < 2 + j % 2;
            uint16_t c = on ? C4[j] : RGB(34, 36, 44);
            cv_rect(16 + i * 13 + (i >> 2) * 2, 8 + j * 16, 11, 12, c);
        }
    {
        int32_t w = text_w(&FONT_L, "GX-1");
        cv_text((240 - w) / 2, 80, &FONT_L, "GX-1", RGB(236, 238, 246));
        w = text_w(&FONT_S, "Genre-aware MIDI composer");
        cv_text((240 - w) / 2, 116, &FONT_S, "Genre-aware MIDI composer", RGB(146, 148, 160));
        w = text_w(&FONT_XS, "v" EN_VERSION);
        cv_text((240 - w) / 2, 134, &FONT_XS, "v" EN_VERSION, RGB(86, 88, 100));
    }
    cv_blit(0, 30);
    lcd_sync();
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0;
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();
    }
    lcd_init();
    palette_set(2);
    splash();
    if (en_dbg.magic != DBG_MAGIC) {
        memset(&en_dbg, 0, sizeof en_dbg);
        en_dbg.magic = DBG_MAGIC;
    }
    en_dbg.in_audio = 0;                            /* (kept through a reset: a crash in the audio interrupt left it
                                                     * set, and USB waits for it to clear; safe mode has no audio) */
    if (safe_mode)
        safe_main();                                /* never returns */
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != en_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        en_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 186, 240, &FONT_S, "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        draw_text_box(0, 206, 240, &FONT_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    en_dbg.boots++;
    en_dbg.max_us = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
    if (flash_ok)
        ls_init();                                  /* the log: its index from what is in flash */
    s4_defaults_glob();
    s4_init();
    project_load();                                 /* the demo when nothing (or another format) is saved */
    audio_init();
    usb_start();
#if EN_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    /* above ALNK0 (3): the key / encoder / LED matrix is scanned one column per tick and must keep
     * its rhythm through the engine's tick (X0X's finding) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u)
        panel_setup();                              /* OCT- + OCT+ held at power-on */
    fm1_delay_ms(400);
    ui_init();

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = (k10 * k10) >> 8;           /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's, unchanged) */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (shown)
                    ui_say("UPDATE MODE", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                s4_panic();
                fm1_delay_ms(5);
                enter_uboot("UBOOT");
            }
        }
        dev_sysex();                                /* SEQ-4's own: the load, asked over USB */
        ota_service();                              /* M-UPGRADE handshake (other SysEx is dropped) */
        if (usb.ota_req) {
            usb.ota_req = 0;
            s4_play(0);
            s4_panic();
            fm1_delay_ms(5);
            if (project_is_dirty())
                project_save_all();
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            ui_init();
        }
        if (usb.uboot_req) {
            s4_panic();
            fm1_delay_ms(5);
            enter_uboot("UBOOT (USB)");
        }
#if EN_CDC
        cdc_task();
#endif
        project_service();                          /* the arranger's and program changes' patterns, tidying */
        project_autosave(!s4p.on && !s4p.count);
        if (fm1_ms - last_frame >= 16u && (int32_t)(fm1_ms - ui_pause_until) >= 0) {   /* ~60 frames/s at most */
            uint32_t t0 = fm1_ticks(), us;
            last_frame = fm1_ms;
            en_dbg.ui_frames++;
            ui_frame();
            us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
            ui_us += us;
            if (us > ui_us_max)
                ui_us_max = us;
        } else {
            ui_input_only();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
