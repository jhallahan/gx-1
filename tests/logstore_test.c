/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of firmware/src/logstore.c against a simulated NOR flash (erase -> 0xFF, programming only
 * clears bits, pages never wrapped): saving and loading, parts, filling and compacting, and a cut at every
 * point of a save (the old version stays until the new one is whole). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* a cut: programming stops after this many more writes */
static uint32_t progs, erases;

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (off & 0xFFFu) {
        printf("unaligned erase %#x\n", off);
        exit(1);
    }
    memset(nor + off, 0xFF, 4096);
    erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    if (off < 0x97000u || off + n > 0xDC000u) {
        printf("outside the region: %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    progs++;
    return 0;
}
#include "../firmware/src/logstore.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void fill(uint8_t *b, uint32_t n, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] = (uint8_t)(seed * 131u + i * 7u + (i >> 8));
}

int main(void)
{
    static uint8_t a[12000], b[12000], got[12000];
    int n, i, k;
    memset(nor, 0xFF, sizeof nor);
    check("an empty store scans", ls_init() == 0);
    check("... and holds nothing", ls_load(5, got, sizeof got) < 0);
    fill(a, 700, 1);
    check("save a small record", ls_save(5, a, 700) == 0);
    n = ls_load(5, got, sizeof got);
    check("load it back", n == 700 && !memcmp(got, a, 700));
    fill(b, 9000, 2);
    check("save a big one (three parts)", ls_save(6, b, 9000) == 0);
    n = ls_load(6, got, sizeof got);
    check("load it back whole", n == 9000 && !memcmp(got, b, 9000));
    check("after a restart: both still there", ls_init() == 0 && ls_load(5, got, sizeof got) == 700 &&
                                                   !memcmp(got, a, 700) && ls_load(6, got, sizeof got) == 9000 &&
                                                   !memcmp(got, b, 9000));
    fill(a, 800, 3);
    check("a new version", ls_save(5, a, 800) == 0 && ls_load(5, got, sizeof got) == 800 && !memcmp(got, a, 800));
    check("deleted", ls_del(5) == 0 && !ls_has(5));
    check("a restart keeps it deleted", ls_init() == 0 && !ls_has(5) && ls_has(6));

    /* many saves: the store fills, compaction makes room (erasing allowed between saves) */
    {
        int ok = 1, gcs = 0;
        for (i = 0; i < 3000 && ok; i++) {
            int key = i % 70, len = 200 + (i * 37) % 3500, rc;
            fill(a, (uint32_t)len, (uint32_t)i);
            rc = ls_save(key, a, (uint32_t)len);
            if (rc == -1) {                    /* no room without erasing: erase, then again */
                int g;
                while ((g = ls_gc()) == 1)
                    gcs++;
                rc = ls_save(key, a, (uint32_t)len);
            }
            if (rc || ls_load(key, got, sizeof got) != len || memcmp(got, a, (size_t)len))
                ok = 0;
            while (ls_gc_due() && ls_gc() == 1)
                gcs++;
        }
        check("3000 saves of 70 keys, compacted as it goes", ok);
        printf("    (%u erases, %u page writes, %d collections, live %u of %u bytes)\n", erases, progs, gcs, ls_live(), ls_capacity());
        ok = ls_init() == 0;
        for (k = 0; k < 70 && ok; k++) {
            int last = 2999 - ((2999 - k) % 70), len = 200 + (last * 37) % 3500;
            fill(a, (uint32_t)len, (uint32_t)last);
            if (ls_load(k, got, sizeof got) != len || memcmp(got, a, (size_t)len))
                ok = 0;
        }
        check("after a restart every key has its last version", ok);
    }

    /* a cut at every write of a three-part save: the old version stays, or the new is whole */
    {
        int ok = 1, cut;
        fill(a, 9500, 77);
        ls_save(9, a, 9500);
        fill(b, 9500, 78);
        for (cut = 0; cut < 60 && ok; cut++) {
            while (ls_gc() == 1)
                ;
            ls_init();
            fail_after = cut;
            ls_save(9, b, 9500);
            fail_after = -1;
            ls_init();
            n = ls_load(9, got, sizeof got);
            if (!(n == 9500 && (!memcmp(got, a, 9500) || !memcmp(got, b, 9500))))
                ok = 0;
            if (n == 9500 && !memcmp(got, b, 9500)) {   /* it went through: back to the old one for the next cut */
                ls_save(9, a, 9500);
            }
            /* and the store still takes saves after the cut */
            fill(got, 300, (uint32_t)cut);
            if (ls_save(10, got, 300) && (ls_gc(), ls_save(10, got, 300)))
                ok = 0;
        }
        check("a cut anywhere in a save: old or new, never broken", ok);
    }

    /* a cut in the middle of a body: that sector is never written over again */
    {
        int ok;
        ls_init();
        fail_after = 3;
        fill(a, 2000, 90);
        ls_save(11, a, 2000);
        fail_after = -1;
        ls_init();
        fill(b, 1500, 91);
        ok = ls_save(12, b, 1500) == 0 || (ls_gc(), ls_save(12, b, 1500) == 0);
        ok = ok && ls_init() == 0 && ls_load(12, got, sizeof got) == 1500 && !memcmp(got, b, 1500);
        check("after a torn body, new saves land on clean flash", ok);
    }
    printf(bad ? "logstore test: %d FAILED\n" : "logstore test passed\n", bad);
    return bad != 0;
}
