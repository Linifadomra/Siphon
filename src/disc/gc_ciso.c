#include "gc_disc_internal.h"
#include "siphon_log.h"
#include <stdlib.h>
#include <string.h>

#define CISO_HEADER_SIZE 0x8000
#define CISO_MAP_OFFSET  8
#define CISO_MAP_SIZE    0x7FF8

typedef struct {
    uint32_t  blockSize;
    uint32_t  blockCount;
    uint8_t   blockMap[CISO_MAP_SIZE];
    uint32_t* presentBefore;
} CISOData;

static int ciso_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    CISOData* cd = (CISOData*)disc->formatData;
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t blockIdx = offset / cd->blockSize;
        uint32_t blockOff = offset % cd->blockSize;
        size_t   chunk    = cd->blockSize - blockOff;
        if (chunk > remaining) chunk = remaining;

        if (blockIdx >= cd->blockCount || !cd->blockMap[blockIdx]) {
            memset(out, 0, chunk);
        } else {
            uint64_t fileOff = (uint64_t)CISO_HEADER_SIZE +
                               (uint64_t)cd->presentBefore[blockIdx] * cd->blockSize +
                               blockOff;
            if (fseek(disc->file, (long)fileOff, SEEK_SET) != 0) return -1;
            if (fread(out, 1, chunk, disc->file) != chunk) return -1;
        }

        out       += chunk;
        offset    += (uint32_t)chunk;
        remaining -= chunk;
    }

    return 0;
}

static void ciso_close(GCDisc* disc) {
    CISOData* cd = (CISOData*)disc->formatData;
    if (cd) {
        free(cd->presentBefore);
        free(cd);
    }
    disc->formatData = NULL;
}

int gc_ciso_open(GCDisc* disc) {
    uint8_t hdr[CISO_HEADER_SIZE];
    if (fseek(disc->file, 0, SEEK_SET) != 0) return -1;
    if (fread(hdr, 1, CISO_HEADER_SIZE, disc->file) != CISO_HEADER_SIZE) {
        siphon_log("CISO header truncated");
        return -1;
    }

    CISOData* cd = (CISOData*)calloc(1, sizeof(CISOData));
    if (!cd) return -1;

    cd->blockSize = gc_le32(hdr + 4);
    if (cd->blockSize == 0) {
        siphon_log("CISO block size is zero");
        free(cd); return -1;
    }

    memcpy(cd->blockMap, hdr + CISO_MAP_OFFSET, CISO_MAP_SIZE);

    cd->blockCount = CISO_MAP_SIZE;
    while (cd->blockCount > 0 && !cd->blockMap[cd->blockCount - 1])
        cd->blockCount--;
    cd->blockCount++;

    cd->presentBefore = (uint32_t*)calloc(cd->blockCount + 1, sizeof(uint32_t));
    if (!cd->presentBefore) { free(cd); return -1; }

    uint32_t count = 0;
    for (uint32_t i = 0; i < cd->blockCount; i++) {
        cd->presentBefore[i] = count;
        if (cd->blockMap[i]) count++;
    }

    disc->formatData = cd;
    disc->read  = ciso_read;
    disc->close = ciso_close;

    return gc_disc_parse_fst(disc);
}
