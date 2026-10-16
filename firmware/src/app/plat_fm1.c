/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 platform on the FM-1: plat.h over Felucca's HAL, panel map, USB rings, storage and the log.
 * Part of the unity build (main.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* Glow: every button not lit is lit dim, on one scan frame in GLOW_DIV (the HAL's fm1_led_dim) */
#define GLOW_DIV 4u
static uint8_t glow_on = 1;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
/* PLAY has two LEDs: its red at the button's own position, a green at the keyless matrix position
 * column 8, row PA5 (found on the device). Green, unless recording. */
static uint8_t play_red;
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY && !play_red)            /* PLAY's green LED instead (keyless position col 8, row PA5) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (i < NB && glow_on)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    fm1_led_dim_div = glow_on ? GLOW_DIV : 0u;
}

static volatile uint32_t audio_cpu_pct, audio_xruns, midi_drops;

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
static uint32_t midi_out_hw;                     /* the USB ring's most in it at once (SysEx 01) */
int plat_midi_out(uint32_t pkt)
{
    uint32_t used = mo_w - mo_r;
    if (used > midi_out_hw)
        midi_out_hw = used;
    return midi_out_event(pkt);
}
int plat_usb_up(void) { return usb.config && !usb.suspended; }
/* the sequencer's MIDI out: a USB-MIDI packet on the port's cable, CIN = the status's high nibble (system
 * real time: CIN F) */
void s4_out(uint32_t port, uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >= 0xF8u ? 0xFu : st >> 4;
    if (st >= 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
        d1 = d2 = 0;
        cin = 0xFu;
    }
    if (!plat_midi_out((port & 3u) << 4 | cin | st << 8 | d1 << 16 | d2 << 24))
        midi_drops++;
}
void s4_lock(int on)                           /* the engine held off while the UI swaps a sequencer whole */
{
    static uint32_t f;
    if (on)
        f = irq_save();
    else
        irq_restore(f);
}

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
static int32_t store_rc[OBJ_NOBJ];               /* each object's last save (the SysEx status reports them) */
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    int rc = flash_ok ? st_save(obj, src, len) : -9;
    if (obj < OBJ_NOBJ)
        store_rc[obj] = rc;
    return rc;
}
static int32_t log_rc;                           /* the log's last save */
int plat_log_load(int key, void *dst, uint32_t max) { return flash_ok ? ls_load(key, dst, max) : -1; }
int plat_log_save(int key, const void *src, uint32_t len)
{
    int rc = flash_ok ? ls_save(key, src, len) : -9;
    log_rc = rc;
    return rc;
}
int plat_log_has(int key) { return flash_ok && ls_has(key); }
int plat_log_gc(void)
{
    if (!flash_ok)
        return 0;
    audio_silence();
    return ls_gc();
}
int plat_log_gc_due(void) { return flash_ok && ls_gc_due(); }
uint32_t plat_log_live(void) { return ls_live(); }
uint32_t plat_log_capacity(void) { return ls_capacity(); }

uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }
