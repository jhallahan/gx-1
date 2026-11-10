/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Order-independent randomness (ADR-004): every draw is a pure function of its coordinates. */
#include "gx.h"

/* the splitmix32-style finaliser (the "lowbias32" constants) */
static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t gx_hash(uint32_t seed, uint32_t role, uint32_t bar, uint32_t step, uint32_t purpose)
{
    /* fold each coordinate through the mixer with distinct odd constants, so that
     * (a, b) and (b, a) never collide by symmetry */
    uint32_t h = mix32(seed ^ 0x9e3779b9U);
    h = mix32(h ^ (role * 0x85ebca6bU + 0x165667b1U));
    h = mix32(h ^ (bar * 0xc2b2ae35U + 0x27d4eb2fU));
    h = mix32(h ^ (step * 0x2545f491U + 0x61c88647U));
    h = mix32(h ^ (purpose * 0x9e3779b1U + 0x7f4a7c15U));
    return h;
}

/* the high 32 bits of a 32 x 32-bit product, in 16-bit pieces: exact, and no 64-bit arithmetic,
 * so the FM-1's 32-bit core needs no 64-bit multiply helper */
static uint32_t mulhi32(uint32_t a, uint32_t b)
{
    uint32_t ah = a >> 16, al = a & 0xffffu, bh = b >> 16, bl = b & 0xffffu;
    uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint32_t mid = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    return hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

uint32_t gx_range(uint32_t h, uint32_t lo, uint32_t hi)
{
    uint32_t span;
    if (hi <= lo)
        return lo;
    span = hi - lo + 1u;
    /* multiply-shift: floor(h * span / 2^32), mapping the full 32-bit hash onto 0..span-1 */
    return lo + mulhi32(h, span);
}

int gx_chance(uint32_t h, uint32_t p255)
{
    if (p255 >= 256u)
        return 1;
    return (h >> 24) < p255;
}

uint32_t gx_role_seed(uint32_t master, gx_role_t role)
{
    return gx_hash(master, (uint32_t)role, 0, 0, GX_P_SEED);
}
