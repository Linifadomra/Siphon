#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "gc_disc_internal.h"
#include "siphon_log.h"
#include <stdlib.h>
#include <string.h>

#define GC_DISC_SIZE  0x57058000u
#define WII_DISC_SIZE 0x118580000u

typedef struct {
    uint32_t  wbfsSectorSize;
    uint16_t* wlbaTable;
    uint32_t  wlbaCount;
} WBFSData;

static int wbfs_read(GCDisc* disc, uint64_t offset, void* buf, size_t size) {
    WBFSData* wb = (WBFSData*)disc->formatData;
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = size;

    while (remaining > 0) {
        uint64_t blockIdx = offset / wb->wbfsSectorSize;
        uint32_t blockOff = (uint32_t)(offset % wb->wbfsSectorSize);
        size_t   chunk    = wb->wbfsSectorSize - blockOff;
        if (chunk > remaining) chunk = remaining;

        if (blockIdx >= wb->wlbaCount || wb->wlbaTable[blockIdx] == 0) {
            memset(out, 0, chunk);
        } else {
            uint64_t fileOff = (uint64_t)wb->wlbaTable[blockIdx] * wb->wbfsSectorSize + blockOff;
            off_t cur = ftello(disc->file);
            if (cur != (off_t)fileOff) {
                if (fseeko(disc->file, (off_t)fileOff, SEEK_SET) != 0) return -1;
            }
            if (fread(out, 1, chunk, disc->file) != chunk) return -1;
        }

        out       += chunk;
        offset    += chunk;
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
        siphon_log("WBFS: header truncated");
        return -1;
    }

    uint8_t hdShift   = hdr[8];
    uint8_t wbfsShift = hdr[9];

    if (hdShift > 30 || wbfsShift > 30) {
        siphon_log("WBFS: invalid sector shifts (hd=%u wbfs=%u)", hdShift, wbfsShift);
        return -1;
    }

    uint32_t hdSectorSize   = 1u << hdShift;
    uint32_t wbfsSectorSize = 1u << wbfsShift;

    if (fseek(disc->file, 0, SEEK_END) != 0) return -1;
    long fileSize = ftell(disc->file);
    if (fileSize < 0) return -1;

    uint64_t wlbaOff = (uint64_t)hdSectorSize + 0x100;
    if ((uint64_t)fileSize <= wlbaOff) {
        siphon_log("WBFS: file too small for WLBA table (size=%ld wlbaOff=0x%llX)",
                   fileSize, (unsigned long long)wlbaOff);
        return -1;
    }

    uint64_t tableBytes = (uint64_t)fileSize - wlbaOff;
    uint32_t wlbaFromFile = (uint32_t)(tableBytes / 2);
    uint32_t wlbaMaxWii = (WII_DISC_SIZE + wbfsSectorSize - 1) / wbfsSectorSize;
    uint32_t wlbaMaxGc  = (GC_DISC_SIZE + wbfsSectorSize - 1) / wbfsSectorSize;
    uint32_t wlbaCount  = wlbaFromFile;
    if (wlbaCount > wlbaMaxWii) wlbaCount = wlbaMaxWii;

    WBFSData* wb = (WBFSData*)calloc(1, sizeof(WBFSData));
    if (!wb) return -1;
    wb->wbfsSectorSize = wbfsSectorSize;
    wb->wlbaCount = wlbaCount;

    wb->wlbaTable = (uint16_t*)calloc(wb->wlbaCount, sizeof(uint16_t));
    if (!wb->wlbaTable) { free(wb); return -1; }

    if (fseek(disc->file, (long)wlbaOff, SEEK_SET) != 0) {
        free(wb->wlbaTable); free(wb); return -1;
    }

    uint8_t* rawTable = (uint8_t*)malloc(wb->wlbaCount * 2);
    if (!rawTable) { free(wb->wlbaTable); free(wb); return -1; }

    if (fread(rawTable, 1, wb->wlbaCount * 2, disc->file) != wb->wlbaCount * 2) {
        siphon_log("WBFS: wlba table truncated (expected %u entries)", wb->wlbaCount);
        free(rawTable); free(wb->wlbaTable); free(wb); return -1;
    }

    for (uint32_t i = 0; i < wb->wlbaCount; i++) {
        wb->wlbaTable[i] = gc_be16(rawTable + i * 2);
    }
    free(rawTable);

    disc->formatData = wb;
    disc->read  = wbfs_read;
    disc->close = wbfs_close;

    if (gc_disc_parse_fst(disc) != 0)
        return -1;

    return 0;
}
