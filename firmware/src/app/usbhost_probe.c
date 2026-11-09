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
    draw_text_box(0, 2, 240, &FONT_S, "GX-1 USB HOST PROBE", C_WHITE, 1);
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

static int uh_try(uint32_t cid)
{
    uint32_t v, dev, t0, intr = 0, err = 0;
    uint8_t d[18];
    int n;

    fm1_usb_reset();
    uh_wait(30);
    FM1_CLK_CON1 &= ~3u;                                 /* USB clock = PLL48M (as the device attach) */
    fm1_usb_ep0_buf(uh_buf);
    FM1_USB_IO_CON0 |= (1u << 10) | (1u << 9);           /* the data inputs on (as the device attach) */
    FM1_USB_IO_CON0 &= ~(1u << 11);
    FM1_USB_IO_CON0 |= 1u << 12;
    FM1_USB_IO_CON0 &= ~0xF0u;
    FM1_USB_IO_CON0 |= 0x30u;                            /* host: 15k pull-downs on D+ and D-, no pull-up */
    FM1_USB_CON0 &= ~0x4035u;
    FM1_USB_CON1 = 0;
    FM1_USB_CON0 |= 1u;                                  /* PHY on */
    FM1_USB_CON0 |= 0x2Cu | (cid << 4);                  /* NRST, TM1, VBUS valid; CID as asked */
    uh_wait(50);

    v = FM1_USB_CON0;
    uh_line(cid ? "CID=1  D+ D-" : "CID=0  D+ D-", ((v >> 17) & 1u) << 4 | ((v >> 16) & 1u), 2,
            (v >> 17) & 1u ? 1 : 2);                     /* 10: a full-speed device on the line */

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
    usbh_mode = 1;                                       /* the device driver stops polling */
    uh_wait(20);
    uh_n = 0;
    uh_line("plug the device in now", 0, 0, 0);
    uh_show();
    uh_wait(3000);                                       /* time to plug the dongle in */
    uh_n = 0;
    if (!uh_try(0))
        uh_try(1);
    uh_line("photo this; power off to leave", 0, 0, 0);
    uh_show();
    for (;;)
        fm1_wdt_feed();
}
