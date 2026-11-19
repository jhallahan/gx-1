/* SPDX-License-Identifier: GPL-3.0-only */
/* GX-1: USB host probe (experimental, stage 1). SEQ held at power-on: instead of GX-1, a screen that tries to
 * run the FM-1's USB port as a HOST (so a USB MIDI device, e.g. a wireless MIDI dongle, could plug into the
 * FM-1 with no computer), and reports each step for a photo. Normal boot is untouched; power off and on to leave.
 *
 * The FM-1's USB0 is a Mentor full-speed OTG core (the MUSB byte map src/usb.c drives as a device). JieLi's
 * host driver is closed, so this follows the standard MUSB host sequence with the pad and control bits from
 * the AC79 SDK's register header (include_lib/driver/cpu/wl82/asm/usb.h, Apache-2.0):
 *   USB_CON0   PHY_ON 0, USB_NRST 2, TM1 3, CID 4 (ID: 1 = B / device), VBUS 5; DM_SE 16, DP_SE 17 (line state)
 *   USB_IO_CON0  DPIE 2, DMIE 3, DPPD 4, DMPD 5 (15k pull-downs: a host), DPPU 6 (1.5k pull-up: a device)
 * and in the core: DEVCTL 0x0F (Session 0, HostMode 2, VBus 3-4, LSDev 5, FSDev 6), POWER reset bit 3,
 * INTRUSB connect bit 4, host CSR0 (RxPktRdy 0, TxPktRdy 1, RxStall 2, SetupPkt 3, Error 4, ReqPkt 5,
 * StatusPkt 6, NAKTimeout 7).
 *
 * Steps shown: 1 the line state with host pull-downs (a full-speed device pulls D+ up), 2 the session and
 * host mode, 3 the connect, 4 a bus reset, 5 GET_DESCRIPTOR(device) on address 0: the vendor and product id.
 * Each ID setting (CID 0 and 1) is tried, as which one the core wants for host mode is not known. */

static volatile uint8_t usbh_mode;               /* the probe owns the USB core: usb_poll stays out */

#define UH_S_DEVCTL 15u
static uint8_t uh_buf[64 + 4] __attribute__((aligned(4)));
static char uh_txt[13][34];
static uint8_t uh_col[13];
static int uh_n;

static void uh_hex(char *d, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < digits; i++)
        d[i] = H[(v >> (4 * (digits - 1 - i))) & 15u];
    d[digits] = 0;
}
static void uh_line(const char *a, uint32_t v, int digits, int ok)
{
    char *t;
    int i = 0;
    if (uh_n >= 13)
        return;
    t = uh_txt[uh_n];
    while (*a && i < 22)
        t[i++] = *a++;
    t[i] = 0;
    if (digits) {
        t[i++] = ' ';
        uh_hex(t + i, v, digits);
    }
    uh_col[uh_n++] = (uint8_t)ok;
}
static void uh_show(void)
{
    int i;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 2, 240, &FONT_S, "GX-1 USB HOST PROBE 2", C_WHITE, 1);
    for (i = 0; i < uh_n; i++)
        draw_text_box(4, 22 + (uint32_t)i * 16u, 232, &FONT_XS, uh_txt[i],
                      uh_col[i] == 1 ? RGB(110, 230, 140) : uh_col[i] == 2 ? RGB(255, 110, 100) : RGB(200, 200, 208), 0);
}
static void uh_wait(uint32_t ms)
{
    uint32_t t0 = fm1_ms;
    while (fm1_ms - t0 < ms)
        fm1_wdt_feed();
}

/* one host EP0 transaction: poll CSR0 until the bits in `done` clear (or `set` appear), or an error */
static int uh_csr0(uint32_t start, uint32_t wait_clear, uint32_t wait_set, uint32_t *csr_out)
{
    uint32_t t0 = fm1_ms, c = 0;
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, start);
    while (fm1_ms - t0 < 300u) {
        fm1_wdt_feed();
        c = sie_rd(S_CSR0);
        if (c & 0x94u)                                   /* RxStall, Error, NAKTimeout */
            break;
        if (wait_set ? (c & wait_set) : !(c & wait_clear)) {
            *csr_out = c;
            return 0;
        }
    }
    *csr_out = c;
    sie_wr(S_CSR0, 0);
    return -1;
}

/* GET_DESCRIPTOR(device): returns bytes read, or -1 (csr in *err) */
static int uh_get_device_desc(uint8_t *out, uint32_t want, uint32_t mps, uint32_t *err)
{
    static const uint8_t setup[8] = {0x80, 6, 0, 1, 0, 0, 18, 0};
    uint32_t got = 0, c, i;
    for (i = 0; i < 8; i++)
        uh_buf[i] = setup[i];
    uh_buf[6] = (uint8_t)want;
    sie_wr(S_FADDR, 0);
    fm1_usb_ep0_send(uh_buf, 8);
    if (uh_csr0(0x0Au, 0x02u, 0, &c)) {                  /* SETUP: SetupPkt + TxPktRdy */
        *err = 0x100u | c;
        return -1;
    }
    while (got < want) {                                 /* DATA IN: ReqPkt until a short packet */
        uint32_t n;
        fm1_usb_ep0_buf(uh_buf);
        if (uh_csr0(0x20u, 0, 0x01u, &c)) {
            *err = 0x200u | c;
            return got ? (int)got : -1;
        }
        n = sie_rd(S_COUNT0);
        fm1_usb_rx_sync();
        for (i = 0; i < n && got < want; i++)
            out[got++] = uh_buf[i];
        sie_wr(S_CSR0, 0);                               /* RxPktRdy cleared */
        if (n < mps)
            break;
    }
    fm1_usb_ep0_send(uh_buf, 0);                         /* STATUS OUT: StatusPkt + TxPktRdy, no data */
    uh_csr0(0x42u, 0x02u, 0, &c);
    *err = c;
    return (int)got;
}

/* stage 2. The first probe (a photo, 2026-11-19) saw D+ D- 00 both times: no device on the line. CID=0 left
 * DEVCTL 00 (no session); CID=1 gave 91 (session, B-device: a peripheral), INTRUSB 04 (a bus reset, as a
 * peripheral sees). So: is the device there at all (the live line, while it's plugged and unplugged), then each
 * ID and VBUS setting, the session held for half a second, and a descriptor read where it looks most like a host. */

/* the live line: 10 s, refreshed; the states seen, counted */
static void uh_live(void)
{
    uint32_t t0 = fm1_ms, last = 0, seen[4] = {0, 0, 0, 0}, v, k;
    char t[34];
    fm1_usb_reset();
    uh_wait(30);
    fm1_usb_host_attach(uh_buf, 0, 1);
    while (fm1_ms - t0 < 10000u) {
        fm1_wdt_feed();
        v = fm1_usb_line_state();
        seen[v & 3u]++;
        if (fm1_ms - last >= 100u) {
            last = fm1_ms;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 2, 240, &FONT_S, "GX-1 USB HOST PROBE 2", C_WHITE, 1);
            draw_text_box(4, 26, 232, &FONT_XS, "1/2 THE LINE: plug and unplug", RGB(200, 200, 208), 0);
            draw_text_box(4, 42, 232, &FONT_XS, "the device a few times now", RGB(200, 200, 208), 0);
            t[0] = 'D'; t[1] = '+'; t[2] = ' '; t[3] = 'D'; t[4] = '-'; t[5] = ' ';
            t[6] = (char)('0' + (v >> 1)); t[7] = (char)('0' + (v & 1u)); t[8] = 0;
            draw_text_box(0, 80, 240, &FONT_S, t, v == 2u ? RGB(110, 230, 140) : v ? RGB(255, 200, 90) : RGB(255, 110, 100), 1);
            for (k = 0; k < 4; k++) {               /* how often each state was seen (a hex count, capped) */
                char *q = t;
                *q++ = (char)('0' + (k >> 1)); *q++ = (char)('0' + (k & 1u)); *q++ = ':'; *q++ = ' ';
                uh_hex(q, seen[k] > 0xFFFFu ? 0xFFFFu : seen[k], 4);
                draw_text_box(60, 120 + k * 16u, 140, &FONT_XS, t, seen[k] ? RGB(200, 200, 208) : RGB(90, 90, 100), 0);
            }
            uh_hex(t, (10000u - (fm1_ms - t0)) / 1000u, 1);
            t[1] = ' '; t[2] = 's'; t[3] = 0;
            draw_text_box(0, 200, 240, &FONT_XS, t, RGB(120, 120, 130), 1);
        }
    }
    uh_n = 0;
    uh_line("line 10 (a device):", seen[2] > 0xFFFFu ? 0xFFFFu : seen[2], 4, seen[2] ? 1 : 2);
    uh_line("line 01 (low speed):", seen[1] > 0xFFFFu ? 0xFFFFu : seen[1], 4, seen[1] ? 1 : 0);
}

/* one ID / VBUS setting: the session asked for and watched for half a second; returns how host-like it got
 * (2: connect or host mode, 1: a session, 0: nothing) */
static int uh_setting(uint32_t cid, uint32_t vbus)
{
    uint32_t t0, dev = 0, intr = 0, line = 0, d;
    char name[8] = "C0 V0:";
    fm1_usb_reset();
    uh_wait(30);
    fm1_usb_host_attach(uh_buf, cid, vbus);
    uh_wait(50);
    sie_wr(UH_S_DEVCTL, 0x01u);                          /* Session */
    t0 = fm1_ms;
    while (fm1_ms - t0 < 500u) {
        fm1_wdt_feed();
        d = sie_rd(UH_S_DEVCTL);
        dev |= d;
        if (!(d & 1u))
            sie_wr(UH_S_DEVCTL, 0x01u);                  /* (asked again while it drops) */
        intr |= sie_rd(S_INTRUSB);
        line |= 1u << fm1_usb_line_state();
    }
    name[1] = (char)('0' + cid);
    name[4] = (char)('0' + vbus);
    /* the line states seen (a bit each: 1 00, 2 01, 4 10), DEVCTL's bits OR'd, INTRUSB's OR'd */
    uh_line(name, line << 16 | dev << 8 | intr, 6, (dev & 4u) || (intr & 0x10u) ? 1 : (dev & 1u) ? 0 : 2);
    return (dev & 4u) || (intr & 0x10u) ? 2 : (dev & 1u) ? 1 : 0;
}

static int uh_try(uint32_t cid, uint32_t vbus)

{
    uint32_t v, dev, t0, intr = 0, err = 0;
    uint8_t d[18];
    int n;

    fm1_usb_reset();
    uh_wait(30);
    fm1_usb_host_attach(uh_buf, cid, vbus);
    uh_wait(50);

    v = fm1_usb_line_state();
    uh_line(cid ? "CID=1  D+ D-" : "CID=0  D+ D-", (v >> 1) << 4 | (v & 1u), 2,
            (v >> 1) ? 1 : 2);                           /* 10: a full-speed device on the line */

    sie_wr(UH_S_DEVCTL, 0x01u);                          /* Session */
    uh_wait(20);
    dev = sie_rd(UH_S_DEVCTL);
    uh_line(" DEVCTL", dev, 2, (dev & 4u) ? 1 : 2);      /* bit 2: host mode */

    t0 = fm1_ms;
    while (fm1_ms - t0 < 600u) {
        fm1_wdt_feed();
        intr |= sie_rd(S_INTRUSB);
        if (intr & 0x10u)
            break;
    }
    uh_line(" INTRUSB (connect=10)", intr, 2, (intr & 0x10u) ? 1 : 2);

    sie_wr(S_POWER, sie_rd(S_POWER) | 0x08u);            /* bus reset 30 ms */
    uh_wait(30);
    sie_wr(S_POWER, sie_rd(S_POWER) & ~0x08u);
    uh_wait(30);
    uh_line(" reset, POWER", sie_rd(S_POWER), 2, 0);

    n = uh_get_device_desc(d, 8, 8, &err);               /* the first 8: bMaxPacketSize0 */
    if (n >= 8) {
        uint32_t mps = d[7] ? d[7] : 8u;
        n = uh_get_device_desc(d, 18, mps, &err);
        if (n >= 12) {
            uh_line(" DEVICE! VID", (uint32_t)d[9] << 8 | d[8], 4, 1);
            uh_line(" PID", (uint32_t)d[11] << 8 | d[10], 4, 1);
            uh_line(" class/mps", (uint32_t)d[4] << 8 | d[7], 4, 1);
            return 1;
        }
    }
    uh_line(" descriptor: fail, csr", err, 3, 2);
    return 0;
}

static void usbhost_probe(void)
{
    static const uint8_t SET[4][2] = {{0, 1}, {0, 0}, {1, 1}, {1, 0}};
    int i, r, best = -1, bestr = -1;
    usbh_mode = 1;                                       /* the device driver stops polling */
    uh_wait(20);
    uh_live();                                           /* 1/2: is the device on the line at all */
    for (i = 0; i < 4; i++) {                            /* 2/2: each setting */
        r = uh_setting(SET[i][0], SET[i][1]);
        if (r > bestr) { bestr = r; best = i; }
    }
    if (uh_n < 9) {                                      /* a descriptor read on the most host-like */
        int keep = uh_n;
        uh_try(SET[best][0], SET[best][1]);
        if (uh_n - keep > 6) uh_n = keep + 6;
    }
    uh_line("photo this; power off", 0, 0, 0);
    uh_show();
    for (;;)
        fm1_wdt_feed();
}
