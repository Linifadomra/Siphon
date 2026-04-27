#include "gc_rvz.h"
#include <string.h>

#define LFG_K 521
#define LFG_J 32
#define LFG_SEED_SIZE 17
#define JUNK_BLOCK_SIZE 0x8000

typedef struct {
    uint32_t buf[LFG_K];
    size_t   pos_bytes;
} LFG;

static uint32_t rd_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint32_t bswap32(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}

static void lfg_forward_step(LFG* g) {
    for (size_t i = 0; i < LFG_J; i++) {
        g->buf[i] ^= g->buf[i + LFG_K - LFG_J];
    }
    for (size_t i = LFG_J; i < LFG_K; i++) {
        g->buf[i] ^= g->buf[i - LFG_J];
    }
}

// LE host only: the bswap pre-bakes the buffer so lfg_get_bytes can memcpy
// straight to BE output.
static void lfg_initialize(LFG* g) {
    for (size_t i = LFG_SEED_SIZE; i < LFG_K; i++) {
        g->buf[i] = (g->buf[i - 17] << 23) ^
                    (g->buf[i - 16] >> 9)  ^
                     g->buf[i - 1];
    }
    for (size_t i = 0; i < LFG_K; i++) {
        uint32_t x = g->buf[i];
        g->buf[i] = bswap32((x & 0xFF00FFFFu) | ((x >> 2) & 0x00FF0000u));
    }
    for (int i = 0; i < 4; i++) lfg_forward_step(g);
}

static void lfg_set_seed(LFG* g, const uint8_t seed[LFG_SEED_SIZE * 4]) {
    g->pos_bytes = 0;
    for (size_t i = 0; i < LFG_SEED_SIZE; i++) {
        g->buf[i] = rd_be32(seed + i * 4);
    }
    for (size_t i = LFG_SEED_SIZE; i < LFG_K; i++) g->buf[i] = 0;
    lfg_initialize(g);
}

static void lfg_get_bytes(LFG* g, size_t count, uint8_t* out) {
    while (count > 0) {
        size_t avail = LFG_K * 4 - g->pos_bytes;
        size_t n = count < avail ? count : avail;
        memcpy(out, (const uint8_t*)g->buf + g->pos_bytes, n);
        g->pos_bytes += n;
        out += n;
        count -= n;
        if (g->pos_bytes == LFG_K * 4) {
            lfg_forward_step(g);
            g->pos_bytes = 0;
        }
    }
}

static void lfg_advance(LFG* g, size_t count) {
    g->pos_bytes += count;
    while (g->pos_bytes >= LFG_K * 4) {
        lfg_forward_step(g);
        g->pos_bytes -= LFG_K * 4;
    }
}

int gc_rvz_unpack(const uint8_t* packed, size_t packed_size,
                  uint64_t data_offset,
                  uint8_t* out, size_t out_cap, size_t* out_len) {
    const uint8_t* p = packed;
    const uint8_t* end = packed + packed_size;
    size_t written = 0;
    uint64_t cur_offset = data_offset;

    while (p < end) {
        if (p + 4 > end) return -1;
        uint32_t size_flag = rd_be32(p);
        p += 4;

        int is_junk = (size_flag & 0x80000000u) != 0;
        uint32_t seg_size = size_flag & 0x7FFFFFFFu;

        if (seg_size > out_cap - written) return -1;

        if (is_junk) {
            if ((size_t)(end - p) < LFG_SEED_SIZE * 4) return -1;
            LFG lfg;
            lfg_set_seed(&lfg, p);
            p += LFG_SEED_SIZE * 4;
            lfg_advance(&lfg, (size_t)(cur_offset % JUNK_BLOCK_SIZE));
            lfg_get_bytes(&lfg, seg_size, out + written);
        } else {
            if ((size_t)(end - p) < seg_size) return -1;
            memcpy(out + written, p, seg_size);
            p += seg_size;
        }

        written   += seg_size;
        cur_offset += seg_size;
    }

    *out_len = written;
    return 0;
}
