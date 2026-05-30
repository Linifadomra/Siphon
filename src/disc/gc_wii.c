#include "gc_disc_internal.h"
#include "siphon_log.h"
#include "aes.h"
#include <stdlib.h>
#include <string.h>

#if defined(SIPHON_USE_COMMONCRYPTO)
#include <CommonCrypto/CommonCrypto.h>
#endif

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
    uint64_t   cachedCluster;
#if !defined(SIPHON_USE_COMMONCRYPTO)
    struct AES_ctx aes;
#endif
} WiiCtx;

static const uint8_t WII_COMMON_KEYS[2][16] = {
    { 0xbe,0xc0,0x7b,0x4e,0x9a,0xe0,0x85,0xbc,0x90,0x56,0x92,0x71,0xcd,0x17,0x79,0x10 },
    { 0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7 },
};

static const uint8_t WII_COMMON_KEY_DEFAULT[16] = {
    0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,
    0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7
};

static int hexnib(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void wii_common_key_by_index(uint8_t idx, uint8_t out[16]) {
    if (idx < 2) {
        memcpy(out, WII_COMMON_KEYS[idx], 16);
    } else {
        memcpy(out, WII_COMMON_KEY_DEFAULT, 16);
    }
}

static int wii_boot_valid(const uint8_t* boot) {
    return boot[0] >= 'A' && boot[0] <= 'Z'
        && boot[1] >= 'A' && boot[1] <= 'Z'
        && boot[2] >= 'A' && boot[2] <= 'Z';
}

static int wii_probe_boot(gc_read_fn raw, GCDisc* disc, uint32_t dataStart,
                          const uint8_t* titleKey, char preview[7]) {
    uint8_t cluster[WII_CLUSTER];
    uint8_t plain[WII_CLUSTER_DATA];
    uint8_t iv[16];

    if (raw(disc, dataStart, cluster, WII_CLUSTER) < 0) return -1;
    memcpy(iv, cluster + 0x3D0, 16);
    memcpy(plain, cluster + WII_CLUSTER_HASH, WII_CLUSTER_DATA);
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, titleKey, iv);
    AES_CBC_decrypt_buffer(&ctx, plain, WII_CLUSTER_DATA);
    memcpy(preview, plain, 6);
    preview[6] = '\0';
    return wii_boot_valid(plain) ? 0 : 1;
}

static int wii_find_title_key(GCDisc* disc, gc_read_fn raw, const uint8_t* ticket,
                              uint32_t dataStart, uint8_t out[16]) {
    const char* hex = getenv("SIPHON_WII_COMMON_KEY");
    if (hex && strlen(hex) >= 32) {
        uint8_t commonKey[16];
        for (int i = 0; i < 16; i++) {
            int hi = hexnib(hex[i*2]), lo = hexnib(hex[i*2+1]);
            if (hi < 0 || lo < 0) break;
            commonKey[i] = (uint8_t)((hi << 4) | lo);
        }
        if (hex[31]) {
            uint8_t iv[16] = {0};
            memcpy(iv, ticket + 0x1DC, 8);
            memcpy(out, ticket + 0x1BF, 16);
            struct AES_ctx ctx;
            AES_init_ctx_iv(&ctx, commonKey, iv);
            AES_CBC_decrypt_buffer(&ctx, out, 16);
            return 0;
        }
    }

    uint8_t iv[16] = {0};
    memcpy(iv, ticket + 0x1DC, 8);

    uint8_t order[3];
    int orderCount = 0;
    uint8_t idx = ticket[0x1F1];
    if (idx < 2) order[orderCount++] = idx;
    if (idx != 1) order[orderCount++] = 1;
    if (idx != 0) order[orderCount++] = 0;

    for (int i = 0; i < orderCount; i++) {
        uint8_t commonKey[16];
        char preview[7];
        wii_common_key_by_index(order[i], commonKey);
        memcpy(out, ticket + 0x1BF, 16);
        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, commonKey, iv);
        AES_CBC_decrypt_buffer(&ctx, out, 16);
        if (wii_probe_boot(raw, disc, dataStart, out, preview) == 0)
            return 0;
    }

    siphon_log("Wii: could not derive a valid title key");
    return -1;
}

static void wii_decrypt_cluster(const uint8_t titleKey[16],
#if !defined(SIPHON_USE_COMMONCRYPTO)
                                struct AES_ctx* aes,
#endif
                                const uint8_t* enc, uint8_t* plain) {
    uint8_t iv[16];
    memcpy(iv, enc + 0x3D0, 16);

#if defined(SIPHON_USE_COMMONCRYPTO)
    size_t outLen = 0;
    CCStatus st = CCCrypt(kCCDecrypt, kCCAlgorithmAES128, 0,
                          titleKey, kCCKeySizeAES128, iv,
                          enc + WII_CLUSTER_HASH, WII_CLUSTER_DATA,
                          plain, WII_CLUSTER_DATA, &outLen);
    if (st != kCCSuccess || outLen != WII_CLUSTER_DATA) {
        siphon_log("Wii: CCCrypt failed status=%d outLen=%zu", (int)st, outLen);
        memset(plain, 0, WII_CLUSTER_DATA);
    }
#else
    memcpy(plain, enc + WII_CLUSTER_HASH, WII_CLUSTER_DATA);
    AES_ctx_set_iv(aes, iv);
    AES_CBC_decrypt_buffer(aes, plain, WII_CLUSTER_DATA);
#endif
}

static int wii_read(GCDisc* disc, uint64_t offset, void* buf, size_t size) {
    WiiCtx* w = (WiiCtx*)disc->wii;
    uint8_t* out = (uint8_t*)buf;

    while (size > 0) {
        uint64_t cluster = offset / WII_CLUSTER_DATA;
        uint32_t intra   = (uint32_t)(offset % WII_CLUSTER_DATA);

        if (w->cachedCluster != cluster) {
            uint64_t phys = (uint64_t)w->dataStart + cluster * (uint64_t)WII_CLUSTER;
            if (w->raw(disc, phys, w->clusterEnc, WII_CLUSTER) < 0) {
                siphon_log("Wii: cluster read failed phys=0x%llX cluster=%llu",
                           (unsigned long long)phys, (unsigned long long)cluster);
                return -1;
            }

            wii_decrypt_cluster(w->titleKey,
#if !defined(SIPHON_USE_COMMONCRYPTO)
                                &w->aes,
#endif
                                w->clusterEnc, w->clusterPlain);
            w->cachedCluster = cluster;
        }

        size_t chunk = WII_CLUSTER_DATA - intra;
        if (chunk > size) chunk = size;
        memcpy(out, w->clusterPlain + intra, chunk);
        out    += chunk;
        offset += chunk;
        size   -= chunk;
    }
    return 0;
}

int gc_wii_wrap(GCDisc* disc) {
    if (disc->format == GC_FORMAT_WIA || disc->format == GC_FORMAT_RVZ)
        return 0;

    uint8_t magic[4];
    if (disc->read(disc, 0x18, magic, 4) < 0) {
        siphon_log("Wii: failed to read disc magic at 0x18");
        return -1;
    }

    if (!(magic[0]==0x5D && magic[1]==0x1C && magic[2]==0x9E && magic[3]==0xA3)) {
        return 0;
    }

    uint8_t grp[32];
    if (disc->read(disc, 0x40000, grp, sizeof(grp)) < 0) {
        siphon_log("Wii: failed to read partition group table at 0x40000");
        return -1;
    }

    uint32_t partOff = 0;
    int found = 0;
    for (int g = 0; g < 4 && !found; g++) {
        uint32_t count   = gc_be32(grp + g*8 + 0);
        uint32_t infoOff = gc_be32(grp + g*8 + 4) << 2;
        if (count > WII_MAX_PARTITIONS) continue;
        for (uint32_t p = 0; p < count; p++) {
            uint8_t pe[8];
            if (disc->read(disc, infoOff + p*8, pe, 8) < 0) {
                siphon_log("Wii: failed reading partition entry g=%d p=%u", g, p);
                return -1;
            }
            uint32_t off  = gc_be32(pe + 0) << 2;
            uint32_t type = gc_be32(pe + 4);
            if (type == 0) { partOff = off; found = 1; break; }
        }
    }
    if (!found) {
        siphon_log("Wii: no type-0 data partition found");
        return -1;
    }

    uint8_t ticket[0x2A4];
    if (disc->read(disc, partOff, ticket, sizeof(ticket)) < 0) {
        siphon_log("Wii: failed to read ticket at partition base");
        return -1;
    }

    WiiCtx* w = (WiiCtx*)calloc(1, sizeof(WiiCtx));
    if (!w) return -1;

    uint8_t dataOffRaw[4];
    if (disc->read(disc, partOff + 0x2B8, dataOffRaw, 4) < 0) {
        siphon_log("Wii: failed to read data offset at partition+0x2B8");
        free(w);
        return -1;
    }
    uint32_t dataOff = gc_be32(dataOffRaw) << 2;
    w->dataStart     = partOff + dataOff;
    w->cachedCluster = (uint64_t)-1;
    w->raw           = disc->read;

    if (wii_find_title_key(disc, disc->read, ticket, w->dataStart, w->titleKey) != 0) {
        free(w);
        return -1;
    }

#if !defined(SIPHON_USE_COMMONCRYPTO)
    AES_init_ctx_key(&w->aes, w->titleKey);
#endif

    disc->wii         = w;
    disc->read        = wii_read;
    disc->offsetShift = 2;
    return 0;
}

void gc_wii_free(GCDisc* disc) {
    if (disc && disc->wii) { free(disc->wii); disc->wii = NULL; }
}
