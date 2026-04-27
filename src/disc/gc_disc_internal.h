#ifndef GC_DISC_INTERNAL_H
#define GC_DISC_INTERNAL_H

#include "gc_disc.h"
#include <stdio.h>

typedef int (*gc_read_fn)(GCDisc* disc, uint32_t offset, void* buf, size_t size);
typedef void (*gc_close_fn)(GCDisc* disc);

struct GCDisc {
    FILE*        file;
    GCDiscFormat format;
    gc_read_fn   read;
    gc_close_fn  close;
    void*        formatData;

    char     gameId[7];
    uint32_t dolOffset;
    uint32_t fstOffset;
    uint32_t fstSize;

    uint8_t  boot[0x440];
    uint8_t  bi2[0x2000];

    uint8_t* apploader;
    size_t   apploaderSize;

    uint8_t* fstData;
    uint32_t entryCount;
    const char* stringTable;

    GCEntry* entries;
    char*    pathBuf;
};

static inline uint16_t gc_be16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static inline uint32_t gc_be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint64_t gc_be64(const uint8_t* p) {
    return (uint64_t)gc_be32(p) << 32 | gc_be32(p + 4);
}

static inline uint32_t gc_le32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int gc_disc_parse_fst(GCDisc* disc);
void gc_disc_free_parsed(GCDisc* disc);

int gc_iso_open(GCDisc* disc);
int gc_ciso_open(GCDisc* disc);
int gc_gcz_open(GCDisc* disc);
int gc_wbfs_open(GCDisc* disc);
int gc_wia_open(GCDisc* disc, int isRVZ);

#endif
