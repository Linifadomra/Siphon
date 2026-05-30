#include "gc_disc_internal.h"
#include "siphon_log.h"
#include "aes.h"
#include <stdlib.h>
#include <string.h>

#define WII_CLUSTER       0x8000u
#define WII_CLUSTER_HASH  0x0400u
#define WII_CLUSTER_DATA  0x7C00u
#define WII_MAX_PARTITIONS 64

typedef struct {
    gc_read_fn raw;
    uint32_t   dataStart;
    uint8_t    titleKey[16];
    uint8_t    clusterEnc[WII_CLUSTER];
    uint8_t    clusterPlain[WII_CLUSTER_DATA];
    uint32_t   cachedCluster;
} WiiCtx;

static const uint8_t WII_COMMON_KEY[16] = {
    0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,
    0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7
};

static int hexnib(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void wii_common_key(uint8_t out[16]) {
    const char* hex = getenv("SIPHON_WII_COMMON_KEY");
    if (hex && strlen(hex) >= 32) {
        for (int i = 0; i < 16; i++) {
            int hi = hexnib(hex[i*2]), lo = hexnib(hex[i*2+1]);
            if (hi < 0 || lo < 0) { memcpy(out, WII_COMMON_KEY, 16); return; }
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return;
    }
    memcpy(out, WII_COMMON_KEY, 16);
}

static void wii_decrypt_title_key(const uint8_t* ticket, uint8_t out[16]) {
    uint8_t commonKey[16];
    uint8_t iv[16] = {0};
    wii_common_key(commonKey);
    memcpy(iv, ticket + 0x1DC, 8);
    memcpy(out, ticket + 0x1BF, 16);
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, commonKey, iv);
    AES_CBC_decrypt_buffer(&ctx, out, 16);
}

static int wii_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    WiiCtx* w = (WiiCtx*)disc->wii;
    uint8_t* out = (uint8_t*)buf;

    while (size > 0) {
        uint32_t cluster = offset / WII_CLUSTER_DATA;
        uint32_t intra   = offset % WII_CLUSTER_DATA;

        if (w->cachedCluster != cluster) {
            uint32_t phys = w->dataStart + cluster * WII_CLUSTER;
            if (w->raw(disc, phys, w->clusterEnc, WII_CLUSTER) < 0) return -1;
            uint8_t iv[16];
            memcpy(iv, w->clusterEnc + 0x3D0, 16);
            memcpy(w->clusterPlain, w->clusterEnc + WII_CLUSTER_HASH, WII_CLUSTER_DATA);
            struct AES_ctx ctx;
            AES_init_ctx_iv(&ctx, w->titleKey, iv);
            AES_CBC_decrypt_buffer(&ctx, w->clusterPlain, WII_CLUSTER_DATA);
            w->cachedCluster = cluster;
        }

        size_t chunk = WII_CLUSTER_DATA - intra;
        if (chunk > size) chunk = size;
        memcpy(out, w->clusterPlain + intra, chunk);
        out    += chunk;
        offset += (uint32_t)chunk;
        size   -= chunk;
    }
    return 0;
}

int gc_wii_wrap(GCDisc* disc) {
    if (disc->format == GC_FORMAT_WIA || disc->format == GC_FORMAT_RVZ) return 0;

    uint8_t magic[4];
    if (disc->read(disc, 0x18, magic, 4) < 0) return -1;
    if (!(magic[0]==0x5D && magic[1]==0x1C && magic[2]==0x9E && magic[3]==0xA3))
        return 0;

    uint8_t grp[32];
    if (disc->read(disc, 0x40000, grp, sizeof(grp)) < 0) return -1;

    uint32_t partOff = 0;
    int found = 0;
    for (int g = 0; g < 4 && !found; g++) {
        uint32_t count   = gc_be32(grp + g*8 + 0);
        uint32_t infoOff = gc_be32(grp + g*8 + 4) << 2;
        if (count > WII_MAX_PARTITIONS) continue;
        for (uint32_t p = 0; p < count; p++) {
            uint8_t pe[8];
            if (disc->read(disc, infoOff + p*8, pe, 8) < 0) return -1;
            uint32_t off  = gc_be32(pe + 0) << 2;
            uint32_t type = gc_be32(pe + 4);
            if (type == 0) { partOff = off; found = 1; break; }
        }
    }
    if (!found) { siphon_log("Wii: no data partition"); return -1; }

    uint8_t ticket[0x2A4];
    if (disc->read(disc, partOff, ticket, sizeof(ticket)) < 0) return -1;

    WiiCtx* w = (WiiCtx*)calloc(1, sizeof(WiiCtx));
    if (!w) return -1;
    wii_decrypt_title_key(ticket, w->titleKey);

    uint8_t phdr[0x1C];
    if (disc->read(disc, partOff + 0x2A4, phdr, sizeof(phdr)) < 0) { free(w); return -1; }
    uint32_t dataOff = gc_be32(phdr + 0x14) << 2;
    w->dataStart     = partOff + dataOff;
    w->cachedCluster = UINT32_MAX;
    w->raw           = disc->read;

    disc->wii         = w;
    disc->read        = wii_read;
    disc->offsetShift = 2;
    return 0;
}

void gc_wii_free(GCDisc* disc) {
    if (disc && disc->wii) { free(disc->wii); disc->wii = NULL; }
}
