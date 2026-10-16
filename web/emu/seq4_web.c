/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 in the browser: the host simulator (host/seq4_host.c: the whole app, UI, engine and store, against a
 * simulated FM-1) compiled to WebAssembly and run in an AudioWorklet (from X0X's, by way of CRT-4's). The
 * worklet asks for audio; producing it advances the device's clock, which runs the engine every 32 samples
 * (the device's audio half) and the UI every 16 ms, as on the FM-1. What the engine plays goes into a ring
 * the worklet empties and the page sends on with WebMIDI; MIDI the page receives comes in through
 * web_midi_in. The flash is kept by the page (its log region and the settings), given back before boot.
 *
 *   web/emu/build.sh   ->  build/emu/seq4.wasm (+ the page) */
#include <stdint.h>

static void web_audio(const int16_t *lr, uint32_t n);
void web_midi_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2);
#include "../../host/seq4_host.c"

#undef __attribute__

/* output ring, stereo float, filled by the ticks, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void web_audio(const int16_t *lr, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)lr[2 * k] / 32768.0f;
        ring[i + 1] = (float)lr[2 * k + 1] / 32768.0f;
    }
}

/* MIDI out: each message with the device's time (microseconds), its port */
#define MRING 2048u
static uint32_t mring[MRING][2];
static uint32_t mr_w, mr_r;
void web_midi_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mr_w - mr_r >= MRING)
        return;
    mring[mr_w % MRING][0] = (uint32_t)now_us;
    mring[mr_w % MRING][1] = (port & 3u) << 24 | (st & 0xFFu) << 16 | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu);
    mr_w++;
}
static uint32_t mout[2 * 256];
__attribute__((used, visibility("default"))) uint32_t *web_midi_buf(void) { return mout; }
/* up to 256 messages into web_midi_buf (time, port << 24 | status << 16 | data1 << 8 | data2): how many */
__attribute__((used, visibility("default"))) uint32_t web_midi_take(void)
{
    uint32_t n = 0;
    while (mr_r != mr_w && n < 256u) {
        mout[2 * n] = mring[mr_r % MRING][0];
        mout[2 * n + 1] = mring[mr_r % MRING][1];
        mr_r++;
        n++;
    }
    return n;
}
/* a message coming in (a USB-MIDI packet: cable 0..3 the ports, 14 the TRS jack) */
__attribute__((used, visibility("default"))) void web_midi_in(uint32_t pkt) { min_q[mi_w++ % MQ] = pkt; }

static float out_l[1024], out_r[1024];
__attribute__((used, visibility("default"))) float *web_out_l(void) { return out_l; }
__attribute__((used, visibility("default"))) float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
__attribute__((used, visibility("default"))) void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

__attribute__((used, visibility("default"))) void web_boot(void) { boot(); }
__attribute__((used, visibility("default"))) void web_buttons(uint32_t m) { held_btn = m; }
__attribute__((used, visibility("default"))) void web_keys(uint32_t m) { held_keys = m; }
__attribute__((used, visibility("default"))) void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
__attribute__((used, visibility("default"))) void web_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
__attribute__((used, visibility("default"))) uint32_t web_lit_buttons(void) { return lit_btn; }
__attribute__((used, visibility("default"))) uint32_t web_lit_keys(void) { return lit_keys; }
__attribute__((used, visibility("default"))) uint16_t *web_fb(void) { return fb; }
__attribute__((used, visibility("default"))) uint32_t web_blits(void) { return blits; }
__attribute__((used, visibility("default"))) uint32_t web_now(void) { return now_ms; }
__attribute__((used, visibility("default"))) uint32_t web_cpu(void) { return cpu_pct; }

/* the flash the page keeps: from the log's start to the end (the log, the panel's and the settings' objects) */
#define KEEP_FROM 0x97000u
__attribute__((used, visibility("default"))) uint8_t *web_flash(void) { return nor + KEEP_FROM; }
__attribute__((used, visibility("default"))) uint32_t web_flash_len(void) { return (uint32_t)sizeof nor - KEEP_FROM; }
__attribute__((used, visibility("default"))) void web_flash_erased(void)   /* before the page's bytes go in */
{
    memset(nor, 0xFF, sizeof nor);
    nor_ready = 1;
}
__attribute__((used, visibility("default"))) uint32_t web_flash_writes(void) { return nor_progs + nor_erases; }
