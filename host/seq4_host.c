/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 on the host: the whole app (UI, project, store, engine) against a simulated FM-1 driven by a script.
 * The engine ticks as on the device (every 32 frames, 0.73 ms); its MIDI goes to a log, with the time of
 * each message to the microsecond; the screen to PNGs, the lights to text, the click to a WAV; the flash is
 * a simulated NOR chip (erased to FF, programming only clears bits), kept across a simulated reboot.
 * (From CRT-4's, LCD-4's, IND-1's, AMB-1's and X0X's simulators.)
 *
 *   seq4_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   hold BTN MS                  press, run MS, release
 *   key K down|up | tapkey K     (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once, exactly N (no acceleration)
 *   master N                     MASTER pot 0..4096
 *   midi B0 B1 B2 [CABLE]        a message coming in (hex bytes; cable 0..3 the USB ports, 14 the TRS jack)
 *   clockin BPM MS               MIDI clock coming in (24 a beat) for MS milliseconds, the device running
 *   wav FILE | wavstop           start / stop recording the click
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys
 *   mark                         the timing measure starts again (note-ons on port A from here)
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound
 *     WHAT: playing ext rec srec count view menu page sel trk mode pat next tempo swing scale root mute snap
 *           song_on cap dirty helpvis    the engine and the UI
 *           notes_on notes_off cc_out pc_out clocks_out starts_out stops_out midi_out drops   MIDI out, counted
 *           last_note last_vel last_ch last_port                                             ... the last note
 *           chnotesC    note-ons on channel C (1..16), any port
 *           velI degI gateI octI condI rtgI ofsI    step I of the sequencer on screen (Mono, Multitrack: its
 *                                                   track; Chord: its chord step)
 *           polyn polyI chordtypeI modL.I len init end div
 *           timing_us   the worst note-on's distance from the sixteenth grid since mark (port A)
 *           grid_n      note-ons counted since mark
 *           store_live store_writes erases       the flash
 *   until WHAT VALUE [MS]        run until the expectation holds (at most MS, default 20000), then check
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   wipe                         an erased flash (then reboot: the demo)
 *   bench K                      a project to measure: 0 all as busy as they get, 1 the demo, 2 a sixteenth line
 *   bootview grid|gen            the view at start (GX-1 opens on GEN; the SEQ-4 scenarios use the grid)
 *   echo TEXT
 *   gx new GENRE SEED [PART]     GX-1: a new idea (deep_dnb, breaks, autonomic) staged on the four sequencers
 *   gx part N | gx compose SEED | gx lock ROLE 0|1 | gx reseed ROLE SEED | gx mutate ROLE MASK INTENSITY
 *   gx macro M V | gx undo | gx redo | gx map tonverk|gm     (ROLE 0 drums, 1 sub, 2 chords, 3 upper)
 */
#define EN_HOST 1
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L          /* clock_gettime, with -std=c99 on Linux */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"

/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"
#include "../firmware/src/seq/seq4.h"

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static double now_us;                   /* the engine's time (its ticks are 0.73 ms apart) */
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
static int glow_on = 1;
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }

#define MQ 1024
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count, notes_on, notes_off, cc_out, pc_out, starts_out, stops_out;
static int last_note = -1, last_vel, last_ch, last_port;
static uint32_t ch_notes[16];
static double grid_t0 = -1, grid_err_max;
static uint32_t grid_n;
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    return 1;
}
int plat_midi_out(uint32_t pkt)
{
    (void)pkt;
    return 1;
}
int plat_usb_up(void) { return 1; }
#ifdef EN_WEB
void web_midi_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2);
#endif
void s4_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_out_count++;
#ifdef EN_WEB
    web_midi_out(port, st, d1, d2);
#endif
    if (st == 0xF8u) {
        clock_out_count++;
        return;
    }
    if (st == 0xFAu || st == 0xFBu)
        starts_out++;
    if (st == 0xFCu)
        stops_out++;
    if ((st & 0xF0u) == 0x90u && d2) {
        notes_on++;
        last_note = (int)d1;
        last_vel = (int)d2;
        last_ch = (int)(st & 15u);
        last_port = (int)port;
        ch_notes[st & 15u]++;
        if (port == 0) {                         /* the timing: each note-on's distance from the grid */
            double step = 60e6 / ((double)s4p.tempo / 10.0) / 4.0;
            if (grid_t0 < 0)
                grid_t0 = now_us;
            else {
                double k = (now_us - grid_t0) / step, e = fabs(k - floor(k + 0.5)) * step;
                if (e > grid_err_max)
                    grid_err_max = e;
            }
            grid_n++;
        }
    } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
        notes_off++;
    else if ((st & 0xF0u) == 0xB0u)
        cc_out++;
    else if ((st & 0xF0u) == 0xC0u)
        pc_out++;
    if (midi_log)
        fprintf(midi_log, "%10.3f ms %c: %02X %02X %02X\n", now_us / 1000.0, 'A' + (int)(port & 3u), st, d1, d2);
}
static uint32_t clicks;
static int32_t ck_env;
void s4_click(int accent)
{
    clicks++;
    ck_env = accent ? 32767 : 22000;
}
void s4_lock(int on) { (void)on; }

/* flash: a simulated NOR chip (the device's st_* hooks over it) */
static uint8_t nor[0x100000];
static uint32_t nor_progs, nor_erases;
static int nor_ready;
static void nor_init(void)
{
    if (!nor_ready)
        memset(nor, 0xFF, sizeof nor);
    nor_ready = 1;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof nor)
        return -8;
    memcpy(dst, nor + off, n);
    return 0;
}
static void audio_silence(void) {}
static int st_erase(uint32_t off)
{
    if (off & 0xFFFu || off + 4096u > sizeof nor)
        return -8;
    memset(nor + off, 0xFF, 4096);
    nor_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off + n > sizeof nor || (off & 0xFFu) + n > 256u) {
        fprintf(stderr, "flash: a write across a page at %#x (%u)\n", off, n);
        return -8;
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    nor_progs++;
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/logstore.c"
static uint32_t store_writes;
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return st_load(obj, dst, max); }
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    store_writes++;
    return st_save(obj, src, len);
}
int plat_log_load(int key, void *dst, uint32_t max) { return ls_load(key, dst, max); }
int plat_log_save(int key, const void *src, uint32_t len)
{
    store_writes++;
    return ls_save(key, src, len);
}
int plat_log_has(int key) { return ls_has(key); }
int plat_log_gc(void) { return ls_gc(); }
int plat_log_gc_due(void) { return ls_gc_due(); }
uint32_t plat_log_live(void) { return ls_live(); }
uint32_t plat_log_capacity(void) { return ls_capacity(); }

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/app/app.h"
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/gx_bridge.c"
#include "../firmware/src/app/ui.c"
#include "../firmware/src/app/bench.c"

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
#define TICK_US (32.0 * 1e6 / 44100.0)
static double tick_due_us, tick_ns_total, tick_budget_ns_total;
static uint32_t ui_due;
static double clk_in_bpm, clk_in_next_us, clk_in_until_us;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    nor_init();
    ls_init();
    s4_defaults_glob();
    s4_init();
    project_load();
    lcd_fill(0, 0, 240, 240, 0);
    ui_init();
}

#ifdef EN_WEB
void web_audio(const int16_t *lr, uint32_t frames);
#endif
static void tick_once(void)
{
    static double last;
    uint32_t pkt, k = 0;
    double t0 = now_ns(), dt;
    int16_t s[64];
    while (k < 64 && plat_midi_in(&pkt)) {
        s4_midi_in(pkt);
        k++;
    }
    s4_tick((uint32_t)(now_us - last + 0.5));
    last = (double)(uint32_t)(now_us + 0.5);
    dt = now_ns() - t0;
    tick_ns_total += dt;
    tick_budget_ns_total += TICK_US * 1000.0;
    cpu_pct = (uint32_t)(100.0 * dt / (TICK_US * 1000.0));
    {                                            /* the click: a decaying 2 kHz tone */
        static double ph;
        uint32_t i;
        for (i = 0; i < 32; i++) {
            double v = ck_env ? sin(ph) * (double)ck_env / 32768.0 * (double)s4g.met_vol / 10.0 * 0.5 : 0.0;
            ph += 2.0 * 3.14159265 * (ck_env > 25000 ? 2000.0 : 1500.0) / 44100.0;
            ck_env = ck_env * 32653 >> 15;
            if (ck_env < 64)
                ck_env = 0;
            s[2 * i] = s[2 * i + 1] = (int16_t)(v * 32767.0);
        }
        if (wav) {
            fwrite(s, 2, 64, wav);
            wav_frames += 32;
        }
#ifdef EN_WEB
        web_audio(s, 32);
#endif
    }
}
static void run_ms(uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < ms; i++) {
        double end = (double)(now_ms + 1) * 1000.0;
        while (tick_due_us <= end) {             /* the engine's ticks, as the device's audio halves */
            while (clk_in_bpm > 0 && clk_in_next_us <= tick_due_us && clk_in_next_us < clk_in_until_us) {
                min_q[mi_w++ % MQ] = 0x0Fu | 0xF8u << 8;   /* a clock coming in (port A) */
                clk_in_next_us += 60e6 / clk_in_bpm / 24.0;
            }
            now_us = tick_due_us;
            tick_once();
            tick_due_us += TICK_US;
        }
        now_ms++;
        now_us = (double)now_ms * 1000.0;
        project_service();
        project_autosave(!s4p.on && !s4p.count);
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int key_of(const char *s)
{
    if (s[0] == 'w')
        return WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

static int step_val(const char *what, int i)    /* a step's value on the sequencer on screen */
{
    const s4_seq_t *q = &s4[sel()];
    i &= 127;
    if (q->mode == M_CHORD) {
        const s4_cstep_t *c = &q->d.chord[i];
        if (!strncmp(what, "vel", 3))
            return c->vel;
        if (!strncmp(what, "deg", 3))
            return c->deg;
        if (!strncmp(what, "gate", 4))
            return c->gate;
        if (!strncmp(what, "oct", 3))
            return c->oct;
        if (!strncmp(what, "cond", 4))
            return c->cond;
        if (!strncmp(what, "rtg", 3))
            return c->rtg;
        if (!strncmp(what, "chordtype", 9))
            return c->type;
        return c->ofs;
    }
    if (q->mode == M_POLY) {
        int k;
        for (k = 0; k < q->d.poly.n; k++)
            if (q->d.poly.note[k].step == i) {
                const s4_pnote_t *p = &q->d.poly.note[k];
                if (!strncmp(what, "vel", 3))
                    return p->vel;
                if (!strncmp(what, "deg", 3))
                    return p->deg;
                if (!strncmp(what, "gate", 4))
                    return p->gate;
                if (!strncmp(what, "oct", 3))
                    return p->oct;
                if (!strncmp(what, "cond", 4))
                    return p->cond;
                if (!strncmp(what, "rtg", 3))
                    return p->rtg;
                return p->ofs;
            }
        return 0;
    }
    {
        const s4_step_t *st = nstep(sel(), trk(), i);
        if (!strncmp(what, "vel", 3))
            return st->vel;
        if (!strncmp(what, "deg", 3))
            return st->deg;
        if (!strncmp(what, "gate", 4))
            return st->gate;
        if (!strncmp(what, "oct", 3))
            return st->oct;
        if (!strncmp(what, "cond", 4))
            return st->cond;
        if (!strncmp(what, "rtg", 3))
            return st->rtg;
        return st->ofs;
    }
}

static int expect_quiet;                        /* until: test without printing */
static int num_after(const char *what, int n) { return atoi(what + n); }
static int expect(const char *what, const char *val)
{
    int got;
    const s4_seq_t *q = &s4[sel()];
#define IS(x) (!strcmp(what, x))
#define PRE(x) (!strncmp(what, x, sizeof x - 1) && what[sizeof x - 1] >= '0' && what[sizeof x - 1] <= '9')
    if (IS("playing"))
        got = s4p.on;
    else if (IS("ext"))
        got = s4p.ext;
    else if (IS("rec"))
        got = s4p.rec;
    else if (IS("srec"))
        got = ui.srec;
    else if (IS("count"))
        got = s4p.count;
    else if (IS("view"))
        got = ui.view;
    else if (IS("menu"))
        got = ui.menu;
    else if (IS("page"))
        got = page();
    else if (IS("sel"))
        got = sel();
    else if (IS("trk"))
        got = trk();
    else if (IS("mode"))
        got = q->mode;
    else if (IS("pat"))
        got = proj.pat[sel()];
    else if (IS("patnow"))
        got = s4p.pat[sel()];
    else if (IS("next"))
        got = s4p.next[sel()] == 0xFF ? -1 : s4p.next[sel()];
    else if (IS("tempo"))
        got = s4g.tempo;
    else if (IS("tempo_now"))
        got = s4p.tempo;
    else if (IS("swing"))
        got = q->swing;
    else if (IS("scale"))
        got = q->scale;
    else if (IS("root"))
        got = q->root;
    else if (IS("mute"))
        got = q->mute;
    else if (IS("snap"))
        got = s4p.snap;
    else if (IS("song_on"))
        got = s4p.song_on;
    else if (IS("cap"))
        got = (int)s4_cap_count;
    else if (IS("dirty"))
        got = ui_dirty();
    else if (IS("helpvis"))
        got = ui.help;
    else if (IS("notes_on"))
        got = (int)notes_on;
    else if (IS("notes_off"))
        got = (int)notes_off;
    else if (IS("cc_out"))
        got = (int)cc_out;
    else if (IS("pc_out"))
        got = (int)pc_out;
    else if (IS("clocks_out"))
        got = (int)clock_out_count;
    else if (IS("starts_out"))
        got = (int)starts_out;
    else if (IS("stops_out"))
        got = (int)stops_out;
    else if (IS("midi_out"))
        got = (int)midi_out_count;
    else if (IS("drops"))
        got = (int)s4p.drops;
    else if (IS("last_note"))
        got = last_note;
    else if (IS("last_vel"))
        got = last_vel;
    else if (IS("last_ch"))
        got = last_ch;
    else if (IS("last_port"))
        got = last_port;
    else if (IS("step"))                       /* the selected sequencer's step playing */
        got = s4p.step[sel()][0];
    else if (IS("step_lag"))                   /* how far it is behind the song's 16ths (a GX-1 phrase: 128) */
        got = (int)(((s4p.pos / 960u) + 128u - s4p.step[sel()][0]) % 128u);
    else if (IS("perf_rpt"))                   /* GX-1's perform: the repeat, the octave, the mutes; the fill */
        got = s4p.perf_rpt;
    else if (IS("perf_oct"))
        got = s4p.perf_oct;
    else if (IS("perf_mute"))
        got = s4p.perf_mute;
    else if (IS("fill"))
        got = s4p.fill;
    else if (IS("gx_locks"))                   /* GX-1: the locks (a bit a track), the part, the page, a macro */
        got = gxb_ok ? (int)gxb_comp.lock : -1;
    else if (IS("gx_part"))
        got = gxb_ok ? gxb_comp.part : -1;
    else if (IS("gx_pg"))
        got = gen.pg;
    else if (PRE("gx_macro"))
        got = gxb_ok ? gxb_comp.r[0].macro[num_after(what, 8) & 7] : -1;
    else if (IS("polyn"))
        got = q->mode == M_POLY ? q->d.poly.n : 0;
    else if (PRE("poly")) {
        int k, i = num_after(what, 4);
        got = 0;
        for (k = 0; q->mode == M_POLY && k < q->d.poly.n; k++)
            got += q->d.poly.note[k].step == i;
    } else if (!strncmp(what, "mx", 2) && strchr(what, '.')) {   /* mxL.I: Matriceal lane L's value I (its track) */
        int l = atoi(what + 2), i = atoi(strchr(what, '.') + 1);
        got = q->d.mx[trk() & 3].v[l % S4_NLANE][i & 15];
    } else if (!strncmp(what, "mod", 3) && strchr(what, '.')) {
        int l = atoi(what + 3), i = atoi(strchr(what, '.') + 1);
        got = q->mod[l & 7][i & 127];
    } else if (PRE("vel") || PRE("deg") || PRE("gate") || PRE("oct") || PRE("cond") || PRE("rtg") || PRE("ofs") ||
               PRE("chordtype")) {
        const char *d = what;
        while (*d && (*d < '0' || *d > '9'))
            d++;
        got = step_val(what, atoi(d));
    } else if (IS("len"))
        got = s4_seq_len(sel(), trk());
    else if (IS("init"))
        got = q->mode == M_MULTI ? q->d.multi.t[trk()].init : q->init;
    else if (IS("end"))
        got = q->mode == M_MULTI ? q->d.multi.t[trk()].end : q->end;
    else if (IS("div"))
        got = q->mode == M_MULTI ? q->d.multi.t[trk()].div : q->div;
    else if (IS("timing_us"))
        got = (int)(grid_err_max + 0.5);
    else if (IS("grid_n"))
        got = (int)grid_n;
    else if (IS("store_live"))
        got = (int)ls_live();
    else if (IS("store_writes"))
        got = (int)store_writes;
    else if (IS("erases"))
        got = (int)nor_erases;
    else if (PRE("chnotes"))
        got = (int)ch_notes[(num_after(what, 7) - 1) & 15];
    else if (IS("clicks"))
        got = (int)clicks;
    else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
#undef IS
#undef PRE
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            if (!expect_quiet)
                printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        if (!expect_quiet)
            printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        if (!expect_quiet)
            printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    if (!expect_quiet)
        printf("  ok %s == %d\n", what, got);
    return 0;
}
static int quiet_expect(const char *what, const char *val)
{
    int r;
    expect_quiet = 1;
    r = expect(what, val);
    expect_quiet = 0;
    return r;
}

static int script_line(const char *line, const char *dir, int lineno, int *fails)
{
    char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0}, out[400];
    if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s", cmd, a, b, c) < 1)
        return 0;
    if (!strcmp(cmd, "wait"))
        run_ms((uint32_t)atoi(a));
    else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap") || !strcmp(cmd, "hold")) {
        int i = find_name(BTN_N, NB, a);
        if (i < 0) {
            printf("line %d: no button %s\n", lineno, a);
            return 2;
        }
        if (cmd[0] != 'r')
            held_btn |= 1u << i;
        if (cmd[0] == 't')
            run_ms(40);
        if (cmd[0] == 'h')
            run_ms((uint32_t)atoi(b));
        if (cmd[0] != 'p')
            held_btn &= ~(1u << i);
        run_ms(cmd[0] == 't' ? 40 : 2);
    } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
        int k = key_of(a);
        if (cmd[0] == 't') {
            held_keys |= 1u << k;
            run_ms(60);
            held_keys &= ~(1u << k);
            run_ms(30);
        } else {
            if (!strcmp(b, "down"))
                held_keys |= 1u << k;
            else
                held_keys &= ~(1u << k);
            run_ms(2);
        }
    } else if (!strcmp(cmd, "turn")) {
        int e = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
        uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
        if (e < 0) {
            printf("line %d: no encoder %s\n", lineno, a);
            return 2;
        }
        for (; n; n -= s) {
            enc_acc[e] += s;
            run_ms(gap);
        }
    } else if (!strcmp(cmd, "spin")) {
        int e = find_name(ENC_N, NE, a);
        if (e < 0) {
            printf("line %d: no encoder %s\n", lineno, a);
            return 2;
        }
        enc_acc[e] += atoi(b);
        accel_off = 1;
        run_ms(1);
        accel_off = 0;
    } else if (!strcmp(cmd, "master"))
        master = (uint32_t)atoi(a);
    else if (!strcmp(cmd, "midi")) {
        uint32_t s = (uint32_t)strtoul(a, 0, 16), d1 = (uint32_t)strtoul(b, 0, 16), d2 = (uint32_t)strtoul(c, 0, 16);
        uint32_t cin = s >= 0xF0 ? 0x0F : s >> 4, cable = 0;
        char e[16] = {0};
        if (sscanf(line, "%*s %*s %*s %*s %15s", e) == 1)
            cable = (uint32_t)atoi(e);
        min_q[mi_w++ % MQ] = cable << 4 | cin | s << 8 | d1 << 16 | d2 << 24;
    } else if (!strcmp(cmd, "gx")) {
        char d[64] = {0}, e2[64] = {0};
        int res = -1;
        sscanf(line, "%*s %*s %*s %*s %63s %63s", d, e2);
        if (!strcmp(a, "new")) {
            res = gxb_new(gx_genre_from_name(b), (uint32_t)strtoul(c, 0, 0));
            if (res >= 0 && d[0])
                res = gxb_part(atoi(d));
        } else if (!strcmp(a, "part")) res = gxb_part(atoi(b));
        else if (!strcmp(a, "compose")) res = gxb_compose((uint32_t)strtoul(b, 0, 0));
        else if (!strcmp(a, "lock")) res = gxb_lock(atoi(b), atoi(c));
        else if (!strcmp(a, "reseed")) res = gxb_reseed(atoi(b), (uint32_t)strtoul(c, 0, 0));
        else if (!strcmp(a, "mutate")) res = gxb_mutate(atoi(b), atoi(c), atoi(d), 0x2545f491u);
        else if (!strcmp(a, "macro")) res = gxb_macro(atoi(b), atoi(c));
        else if (!strcmp(a, "undo")) res = gxb_undo();
        else if (!strcmp(a, "redo")) res = gxb_redo();
        else if (!strcmp(a, "map")) res = gxb_set_map(!strcmp(b, "gm") ? GX_MAP_GM : GX_MAP_TONVERK_SPLIT);
        else if (!strcmp(a, "dump")) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s", dir, b);
            res = gxb_dump(path);
        }
        if (res < 0) {
            fprintf(stderr, "seq4_host: %s: failed\n", line);
            exit(1);
        }
        run_ms(2);                             /* the engine takes the queued patterns */
    } else if (!strcmp(cmd, "clockin")) {
        clk_in_bpm = atof(a);
        clk_in_next_us = now_us;
        clk_in_until_us = now_us + atof(b) * 1000.0;
        run_ms((uint32_t)atoi(b));
        clk_in_bpm = 0;
    } else if (!strcmp(cmd, "wav")) {
        snprintf(out, sizeof out, "%s/%s", dir, a);
        wav_open(out);
    } else if (!strcmp(cmd, "wavstop"))
        wav_close();
    else if (!strcmp(cmd, "shot")) {
        snprintf(out, sizeof out, "%s/%s", dir, a);
        png_write(out);
    } else if (!strcmp(cmd, "leds")) {
        int i;
        printf("  leds: buttons");
        for (i = 0; i < NB; i++)
            if (lit_btn >> i & 1u)
                printf(" %s", BTN_N[i]);
        printf(" | white ");
        for (i = 0; i < 16; i++)
            printf("%c", (lit_keys >> WHITE_K[i] & 1u) ? '#' : '.');
        printf(" black ");
        for (i = 0; i < 11; i++)
            printf("%c", (lit_keys >> BLACK_K[i] & 1u) ? '#' : '.');
        printf("\n");
    } else if (!strcmp(cmd, "mark")) {
        grid_t0 = -1;
        grid_err_max = 0;
        grid_n = 0;
    } else if (!strcmp(cmd, "expect"))
        *fails += expect(a, b);
    else if (!strcmp(cmd, "until")) {           /* until WHAT VALUE [MS]: run until it holds, then check */
        uint32_t lim = c[0] ? (uint32_t)atoi(c) : 20000u, t;
        for (t = 0; t < lim && quiet_expect(a, b); t += 10)
            run_ms(10);
        *fails += expect(a, b);
    } else if (!strcmp(cmd, "reboot")) {
        memset(&proj, 0, sizeof proj);
        memset(s4, 0, sizeof s4);
        boot();
    } else if (!strcmp(cmd, "bench")) {          /* bench K: a project to measure (as SysEx 02 on the device) */
        seq4_bench(atoi(a));
    } else if (!strcmp(cmd, "wipe")) {
        memset(nor, 0xFF, sizeof nor);
    } else if (!strcmp(cmd, "bootview")) {     /* the view at start, from now and after a reboot: grid | gen */
        ui_boot_view = !strcmp(a, "gen") ? V_GEN : V_GRID;
        set_view(ui_boot_view);
    } else if (!strcmp(cmd, "echo"))
        printf("%s", line + 5);
    else {
        printf("line %d: unknown command %s\n", lineno, cmd);
        return 2;
    }
    return 0;
}

#ifndef EN_WEB
int main(int argc, char **argv)
{
    FILE *sc;
    char line[512], out[400];
    const char *dir = argc > 2 ? argv[2] : ".";
    int lineno = 0, fails = 0, r;
    if (argc < 2) {
        fprintf(stderr, "usage: seq4_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    boot();
    while (fgets(line, sizeof line, sc)) {
        lineno++;
        if ((r = script_line(line, dir, lineno, &fails)) != 0)
            return r;
    }
    wav_close();
    if (midi_log)
        fclose(midi_log);
    printf("host: %u ms simulated, %u blits, %u MIDI out, the engine %.2f%% of real time (host CPU)%s\n", now_ms,
           blits, midi_out_count, tick_budget_ns_total > 0 ? 100.0 * tick_ns_total / tick_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* EN_WEB */
