/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4's store: a log in flash for the project, its 64 patterns and the working copies.
 *
 * The region (0x97000 .. 0xDBFFF, 69 sectors of 4 KiB) is written as a log: records are only ever added to
 * erased flash, each a key (what it is), a version, a part (a pattern bigger than a sector goes in parts),
 * its length and a CRC over it all; a key's newest version whose parts are all there and sound is what it
 * holds. A record is written body first, header last, so a cut-off write is never mistaken for one.
 * Adding a record only programs flash (256-byte pages, each a short pause), so saving goes on while
 * playing; erasing (a long pause) waits for when it is allowed: a sector whose records have all been
 * replaced is erased whole, and when few erased sectors are left the emptiest one's live records are
 * copied ahead and it is erased (compaction).
 *
 * Flash access goes through storage.c's hooks: st_read, st_erase, st_prog. */
#define LS_BASE 0x97000u
#define LS_NSECT 69u
#define LS_SECT 4096u
#define LS_SMAGIC 0x4C533453u          /* "S4SL": a sector in use */
#define LS_RMAGIC 0x5334u              /* "4S": a record */
#define LS_HDR 16u
#define LS_MAXPART (LS_SECT - LS_HDR - LS_HDR)
#define LS_NKEY 80
#define LS_MAXP 4                      /* parts a version */
#define LS_RESERVE 3u                  /* erased sectors kept for compaction */

typedef struct {
    uint32_t magic, erases, rsv[2];
} ls_shdr_t;
typedef struct {
    uint16_t magic;
    uint8_t key, part, nparts, rsv;
    uint16_t len;
    uint32_t ver, crc;                 /* crc: the header's first 12 bytes, then the body */
} ls_rhdr_t;

typedef struct {
    uint32_t ver;
    uint8_t nparts, have;              /* parts expected, a bit each part found */
    uint16_t len[LS_MAXP];
    uint32_t at[LS_MAXP];              /* each part's record (flash offset of its header) */
} ls_ver_t;
static struct {
    ls_ver_t v[2];                     /* the newest version seen, and the one before */
} ls_idx[LS_NKEY];
static uint16_t ls_used_b[LS_NSECT];   /* bytes used in each sector (headers included) */
static uint16_t ls_live_b[LS_NSECT];   /* ... that still count */
static uint32_t ls_erases[LS_NSECT];
static uint8_t ls_state[LS_NSECT];     /* 0 erased, 1 in use, 2 broken (to erase) */
static uint32_t ls_head = 0xFFFFFFFFu; /* the sector being written */
static uint32_t ls_gver;               /* the newest version anywhere */
static uint8_t ls_buf[256] __attribute__((aligned(4)));

static uint32_t ls_sec_off(uint32_t sec) { return LS_BASE + sec * LS_SECT; }
static uint32_t ls_round(uint32_t n) { return (n + 15u) & ~15u; }

static uint32_t ls_crc_upd(uint32_t c, const uint8_t *b, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}

/* a version complete: every part found */
static int ls_complete(const ls_ver_t *v) { return v->ver && v->nparts && v->have == (uint8_t)((1u << v->nparts) - 1u); }
static const ls_ver_t *ls_best(int key)
{
    if (key < 0 || key >= LS_NKEY)
        return 0;
    if (ls_complete(&ls_idx[key].v[0]))
        return &ls_idx[key].v[0];
    if (ls_complete(&ls_idx[key].v[1]))
        return &ls_idx[key].v[1];
    return 0;
}

static void ls_note(const ls_rhdr_t *h, uint32_t at)   /* a sound record found: into the index */
{
    ls_ver_t *a = &ls_idx[h->key].v[0], *b = &ls_idx[h->key].v[1], *v = 0;
    if (h->ver > ls_gver)
        ls_gver = h->ver;
    if (h->ver == a->ver)
        v = a;
    else if (h->ver == b->ver)
        v = b;
    else if (h->ver > a->ver) {
        *b = *a;
        v = a;
        v->ver = h->ver;
        v->have = 0;
    } else if (h->ver > b->ver) {
        v = b;
        v->ver = h->ver;
        v->have = 0;
    } else
        return;                        /* older than both: no use */
    v->nparts = h->nparts;
    if (h->part < LS_MAXP) {
        v->have |= (uint8_t)(1u << h->part);
        v->len[h->part] = h->len;
        v->at[h->part] = at;
    }
}

/* what each sector still holds: the parts of each key's version in use */
static void ls_count_live(void)
{
    int k, p;
    uint32_t s;
    for (s = 0; s < LS_NSECT; s++)
        ls_live_b[s] = 0;
    for (k = 0; k < LS_NKEY; k++) {
        const ls_ver_t *v = ls_best(k);
        if (!v)
            continue;
        for (p = 0; p < v->nparts; p++) {
            uint32_t sec = (v->at[p] - LS_BASE) / LS_SECT;
            if (sec < LS_NSECT)
                ls_live_b[sec] = (uint16_t)(ls_live_b[sec] + LS_HDR + ls_round(v->len[p]));
        }
    }
}

static int ls_body_ok(uint32_t at, const ls_rhdr_t *h)
{
    uint32_t c = ls_crc_upd(0xFFFFFFFFu, (const uint8_t *)h, 12u), off = 0;
    while (off < h->len) {
        uint32_t n = h->len - off > sizeof ls_buf ? sizeof ls_buf : h->len - off;
        if (st_read(at + LS_HDR + off, ls_buf, n))
            return 0;
        c = ls_crc_upd(c, ls_buf, n);
        off += n;
    }
    return ~c == h->crc;
}

int ls_init(void)
{
    uint32_t s;
    int k;
    for (k = 0; k < LS_NKEY; k++)
        ls_idx[k].v[0].ver = ls_idx[k].v[1].ver = 0;
    ls_gver = 0;
    ls_head = 0xFFFFFFFFu;
    for (s = 0; s < LS_NSECT; s++) {
        ls_shdr_t sh;
        uint32_t off = LS_HDR, base = ls_sec_off(s);
        ls_used_b[s] = 0;
        ls_erases[s] = 0;
        if (st_read(base, &sh, sizeof sh))
            return -1;
        if (sh.magic == 0xFFFFFFFFu && sh.erases == 0xFFFFFFFFu) {
            uint32_t i, blank = 1;     /* erased: all of it (a torn erase can leave bits) */
            for (i = 0; i < LS_SECT && blank; i += sizeof ls_buf) {
                uint32_t j;
                st_read(base + i, ls_buf, sizeof ls_buf);
                for (j = 0; j < sizeof ls_buf; j++)
                    if (ls_buf[j] != 0xFF)
                        blank = 0;
            }
            ls_state[s] = blank ? 0 : 2;
            continue;
        }
        if (sh.magic != LS_SMAGIC) {
            ls_state[s] = 2;
            continue;
        }
        ls_state[s] = 1;
        ls_erases[s] = sh.erases;
        while (off + LS_HDR <= LS_SECT) {
            ls_rhdr_t h;
            st_read(base + off, &h, sizeof h);
            if (h.magic == 0xFFFFu && h.len == 0xFFFFu && h.ver == 0xFFFFFFFFu)
                break;                 /* the end of what was written */
            if (h.magic != LS_RMAGIC || h.len > LS_MAXPART || off + LS_HDR + h.len > LS_SECT) {
                off = LS_SECT;         /* garbage (a torn header): nothing more here is trusted */
                break;
            }
            if (h.key < LS_NKEY && ls_body_ok(base + off, &h))
                ls_note(&h, base + off);
            off += LS_HDR + ls_round(h.len);
        }
        if (off < LS_SECT) {           /* the rest must be erased (a write cut off in its body leaves bits) */
            uint32_t i;
            for (i = off; i < LS_SECT && off < LS_SECT; i += sizeof ls_buf) {
                uint32_t j, n = LS_SECT - i > sizeof ls_buf ? sizeof ls_buf : LS_SECT - i;
                st_read(base + i, ls_buf, n);
                for (j = 0; j < n; j++)
                    if (ls_buf[j] != 0xFF) {
                        off = LS_SECT;     /* closed: written no more */
                        break;
                    }
            }
        }
        ls_used_b[s] = (uint16_t)off;
    }
    ls_count_live();
    {   /* where to go on writing: the sector in use with the most room left, written last */
        uint32_t best = 0xFFFFFFFFu, room = 0;
        for (s = 0; s < LS_NSECT; s++)
            if (ls_state[s] == 1 && LS_SECT - ls_used_b[s] > room) {
                room = LS_SECT - ls_used_b[s];
                best = s;
            }
        ls_head = room >= 64u ? best : 0xFFFFFFFFu;
    }
    return 0;
}

static uint32_t ls_free_sectors(void)
{
    uint32_t s, n = 0;
    for (s = 0; s < LS_NSECT; s++)
        n += ls_state[s] == 0;
    return n;
}

static int ls_open_sector(uint32_t sec)        /* an erased sector begins its life */
{
    ls_shdr_t sh;
    sh.magic = LS_SMAGIC;
    sh.erases = ls_erases[sec] + 1u;
    sh.rsv[0] = sh.rsv[1] = 0xFFFFFFFFu;
    if (st_prog(ls_sec_off(sec), &sh, sizeof sh))
        return -1;
    ls_erases[sec] = sh.erases;
    ls_state[sec] = 1;
    ls_used_b[sec] = LS_HDR;
    ls_live_b[sec] = 0;
    return 0;
}

/* room for a record of n bytes: the head sector, or the next erased one (keeping the reserve unless
 * compacting) */
static int ls_room(uint32_t n, int compacting)
{
    uint32_t need = LS_HDR + ls_round(n), s, k;
    if (ls_head < LS_NSECT && ls_state[ls_head] == 1 && ls_used_b[ls_head] + need <= LS_SECT)
        return 0;
    if (!compacting && ls_free_sectors() <= LS_RESERVE)
        return -1;
    for (k = 1; k <= LS_NSECT; k++) {          /* the next erased sector after the head (wear spread round) */
        s = ((ls_head < LS_NSECT ? ls_head : 0u) + k) % LS_NSECT;
        if (ls_state[s] == 0) {
            if (ls_open_sector(s))
                return -2;
            ls_head = s;
            return 0;
        }
    }
    return -1;
}

static int ls_prog_span(uint32_t at, const uint8_t *src, uint32_t n)   /* never across a 256-byte page */
{
    while (n) {
        uint32_t k = 256u - (at & 255u), i;
        if (k > n)
            k = n;
        for (i = 0; i < k; i++)
            ls_buf[i] = src[i];        /* (the flash driver wants RAM sources) */
        if (st_prog(at, ls_buf, k))
            return -2;
        at += k;
        src += k;
        n -= k;
    }
    return 0;
}

static int ls_put(int key, int part, int nparts, uint32_t ver, const uint8_t *body, uint32_t len, int compacting)
{
    ls_rhdr_t h;
    uint32_t at, c;
    int rc = ls_room(len, compacting);
    if (rc)
        return rc;
    at = ls_sec_off(ls_head) + ls_used_b[ls_head];
    h.magic = LS_RMAGIC;
    h.key = (uint8_t)key;
    h.part = (uint8_t)part;
    h.nparts = (uint8_t)nparts;
    h.rsv = 0xFF;
    h.len = (uint16_t)len;
    h.ver = ver;
    c = ls_crc_upd(0xFFFFFFFFu, (const uint8_t *)&h, 12u);
    h.crc = ~ls_crc_upd(c, body, len);
    ls_used_b[ls_head] = (uint16_t)(ls_used_b[ls_head] + LS_HDR + ls_round(len));
    if (len && ls_prog_span(at + LS_HDR, body, len))
        return -2;
    if (ls_prog_span(at, (const uint8_t *)&h, sizeof h))   /* the header last: now it counts */
        return -2;
    {   /* read back: a failing part must not report saved */
        ls_rhdr_t chk;
        if (st_read(at, &chk, sizeof chk) || chk.crc != h.crc || chk.ver != ver || !ls_body_ok(at, &chk))
            return -2;
    }
    ls_note(&h, at);
    return 0;
}

int ls_save(int key, const void *src, uint32_t len)
{
    uint32_t ver, nparts, p, off = 0, need = 0;
    int rc;
    if (key < 0 || key >= LS_NKEY)
        return -3;
    nparts = len ? (len + LS_MAXPART - 1u) / LS_MAXPART : 1u;
    if (nparts > LS_MAXP)
        return -3;
    for (p = 0; p < nparts; p++)               /* room for all of it before any of it */
        need += LS_HDR + ls_round(len - p * LS_MAXPART > LS_MAXPART ? LS_MAXPART : len - p * LS_MAXPART);
    if (ls_head >= LS_NSECT || ls_used_b[ls_head] + need > LS_SECT)
        if (ls_free_sectors() <= LS_RESERVE + (need > LS_SECT - 2u * LS_HDR ? nparts : 0u))
            return -1;
    ver = ++ls_gver;
    for (p = 0; p < nparts; p++) {
        uint32_t n = len - off > LS_MAXPART ? LS_MAXPART : len - off;
        if ((rc = ls_put(key, (int)p, (int)nparts, ver, (const uint8_t *)src + off, n, 0)) != 0)
            return rc;
        off += n;
    }
    ls_count_live();
    return 0;
}
int ls_del(int key) { return ls_save(key, "", 0); }

int ls_load(int key, void *dst, uint32_t max)
{
    const ls_ver_t *v = ls_best(key);
    uint32_t off = 0;
    int p;
    if (!v)
        return -1;
    for (p = 0; p < v->nparts; p++) {
        uint32_t n = v->len[p];
        if (off + n > max)
            return -2;
        if (st_read(v->at[p] + LS_HDR, (uint8_t *)dst + off, n))
            return -2;
        off += n;
    }
    return (int)off;
}
int ls_has(int key)
{
    const ls_ver_t *v = ls_best(key);
    return v && (v->nparts > 1 || v->len[0] > 0);
}

uint32_t ls_live(void)
{
    uint32_t s, n = 0;
    for (s = 0; s < LS_NSECT; s++)
        n += ls_live_b[s];
    return n;
}
uint32_t ls_capacity(void) { return (LS_NSECT - LS_RESERVE) * (LS_SECT - LS_HDR); }

/* the work that needs erasing, a sector at a time (when it is allowed: stopped and idle). 1: one done */
int ls_gc(void)
{
    uint32_t s, victim = 0xFFFFFFFFu, least = 0xFFFFFFFFu;
    for (s = 0; s < LS_NSECT; s++)             /* broken ones, and ones with nothing left in them */
        if (ls_state[s] == 2 || (ls_state[s] == 1 && s != ls_head && ls_live_b[s] == 0)) {
            if (st_erase(ls_sec_off(s)))
                return -1;
            ls_state[s] = 0;
            ls_used_b[s] = 0;
            return 1;
        }
    if (ls_free_sectors() > LS_RESERVE + 1u)
        return 0;
    for (s = 0; s < LS_NSECT; s++)             /* compaction: the emptiest full sector, its live records ahead */
        if (ls_state[s] == 1 && s != ls_head && ls_live_b[s] < least && ls_used_b[s] > LS_SECT / 2u) {
            least = ls_live_b[s];
            victim = s;
        }
    if (victim == 0xFFFFFFFFu || least > LS_SECT - 2u * LS_HDR)
        return 0;
    {
        int k, p;
        uint32_t base = ls_sec_off(victim);
        static uint8_t body[LS_MAXPART] __attribute__((aligned(4)));
        for (k = 0; k < LS_NKEY; k++) {
            const ls_ver_t *v = ls_best(k);
            if (!v)
                continue;
            for (p = 0; p < v->nparts; p++)
                if (v->at[p] >= base && v->at[p] < base + LS_SECT) {
                    uint32_t ver = v->ver, nparts = v->nparts, len = v->len[p];
                    if (st_read(v->at[p] + LS_HDR, body, len))
                        return -1;
                    if (ls_put(k, p, (int)nparts, ver, body, len, 1))   /* the same version, elsewhere */
                        return -1;
                }
        }
        ls_count_live();
        if (ls_live_b[victim])
            return -1;
        if (st_erase(base))
            return -1;
        ls_state[victim] = 0;
        ls_used_b[victim] = 0;
        return 1;
    }
}
int ls_gc_due(void)                            /* erasing would help: something to erase, or the reserve low */
{
    uint32_t s;
    for (s = 0; s < LS_NSECT; s++)
        if (ls_state[s] == 2 || (ls_state[s] == 1 && s != ls_head && ls_live_b[s] == 0))
            return 1;
    return ls_free_sectors() <= LS_RESERVE + 1u;
}
