#include "gc_disc_internal.h"
#include <stdlib.h>
#include <string.h>

#define GC_DISC_SIZE 0x57058000u

typedef struct {
    uint32_t  wbfsSectorSize;
    uint16_t* wlbaTable;
    uint32_t  wlbaCount;
} WBFSData;

static int wbfs_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    WBFSData* wb = (WBFSData*)disc->formatData;
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t blockIdx = offset / wb->wbfsSectorSize;
        uint32_t blockOff = offset % wb->wbfsSectorSize;
        size_t   chunk    = wb->wbfsSectorSize - blockOff;
        if (chunk > remaining) chunk = remaining;

        if (blockIdx >= wb->wlbaCount || wb->wlbaTable[blockIdx] == 0) {
            memset(out, 0, chunk);
        } else {
            uint64_t fileOff = (uint64_t)wb->wlbaTable[blockIdx] * wb->wbfsSectorSize + blockOff;
            if (fseek(disc->file, (long)fileOff, SEEK_SET) != 0) return -1;
            if (fread(out, 1, chunk, disc->file) != chunk) return -1;
        }

        out       += chunk;
        offset    += (uint32_t)chunk;
        remaining -= chunk;
    }

    return 0;
}

static void wbfs_close(GCDisc* disc) {
    WBFSData* wb = (WBFSData*)disc->formatData;
    if (wb) {
        free(wb->wlbaTable);
        free(wb);
    }
    disc->formatData = NULL;
}

int gc_wbfs_open(GCDisc* disc) {
    uint8_t hdr[12];
    if (fseek(disc->file, 0, SEEK_SET) != 0) return -1;
    if (fread(hdr, 1, 12, disc->file) != 12) {
        fprintf(stderr, "siphon: WBFS header truncated\n");
        return -1;
    }

    uint8_t hdShift   = hdr[8];
    uint8_t wbfsShift = hdr[9];

    if (hdShift > 30 || wbfsShift > 30) {
        fprintf(stderr, "siphon: WBFS invalid sector shifts (hd=%u wbfs=%u)\n", hdShift, wbfsShift);
        return -1;
    }

    uint32_t hdSectorSize   = 1u << hdShift;
    uint32_t wbfsSectorSize = 1u << wbfsShift;

    WBFSData* wb = (WBFSData*)calloc(1, sizeof(WBFSData));
    if (!wb) return -1;
    wb->wbfsSectorSize = wbfsSectorSize;

    wb->wlbaCount = (GC_DISC_SIZE + wbfsSectorSize - 1) / wbfsSectorSize;
    if (wb->wlbaCount == 0) { free(wb); return -1; }

    wb->wlbaTable = (uint16_t*)calloc(wb->wlbaCount, sizeof(uint16_t));
    if (!wb->wlbaTable) { free(wb); return -1; }

    uint64_t wlbaOff = (uint64_t)hdSectorSize + 0x100;
    if (fseek(disc->file, (long)wlbaOff, SEEK_SET) != 0) {
        free(wb->wlbaTable); free(wb); return -1;
    }

    uint8_t* rawTable = (uint8_t*)malloc(wb->wlbaCount * 2);
    if (!rawTable) { free(wb->wlbaTable); free(wb); return -1; }

    if (fread(rawTable, 1, wb->wlbaCount * 2, disc->file) != wb->wlbaCount * 2) {
        fprintf(stderr, "siphon: WBFS wlba table truncated\n");
        free(rawTable); free(wb->wlbaTable); free(wb); return -1;
    }

    for (uint32_t i = 0; i < wb->wlbaCount; i++) {
        wb->wlbaTable[i] = gc_be16(rawTable + i * 2);
    }
    free(rawTable);

    disc->formatData = wb;
    disc->read  = wbfs_read;
    disc->close = wbfs_close;

    if (gc_disc_parse_fst(disc) != 0) return -1;

    // WBFS disc_size is fixed at GC single-layer size; a Wii or dual-layer disc
    // won't have its real boot ID parse correctly. Cheap sanity: game ID starts
    // with a letter A-Z.
    if (disc->gameId[0] < 'A' || disc->gameId[0] > 'Z') {
        fprintf(stderr, "siphon: WBFS disc doesn't look like a GC game (id='%s'); "
                        "siphon assumes GC single-layer (0x%X bytes)\n",
                        disc->gameId, GC_DISC_SIZE);
        return -1;
    }

    return 0;
}
