#include "gc_disc_internal.h"
#include "siphon_log.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    uint32_t  blockSize;
    uint32_t  numBlocks;
    uint64_t  dataSize;
    uint64_t* blockPtrs;
    uint8_t*  decompBuf;
    uint32_t  cachedBlock;
} GCZData;

static uint64_t gc_le64(const uint8_t* p) {
    return (uint64_t)gc_le32(p) | (uint64_t)gc_le32(p + 4) << 32;
}

static int gcz_decompress_block(GCDisc* disc, GCZData* gz, uint32_t blockIdx) {
    if (gz->cachedBlock == blockIdx) return 0;

    uint64_t ptr = gz->blockPtrs[blockIdx];
    int uncompressed = (ptr >> 63) & 1;
    uint64_t fileOff = ptr & 0x7FFFFFFFFFFFFFFFULL;

    uint64_t nextOff;
    if (blockIdx + 1 < gz->numBlocks) {
        nextOff = gz->blockPtrs[blockIdx + 1] & 0x7FFFFFFFFFFFFFFFULL;
    } else {
        fseek(disc->file, 0, SEEK_END);
        nextOff = (uint64_t)ftell(disc->file);
    }
    size_t compSize = (size_t)(nextOff - fileOff);

    if (fseek(disc->file, (long)fileOff, SEEK_SET) != 0) return -1;

    if (uncompressed) {
        size_t toRead = gz->blockSize;
        if (blockIdx == gz->numBlocks - 1) {
            size_t lastSize = (size_t)(gz->dataSize % gz->blockSize);
            if (lastSize > 0) toRead = lastSize;
        }
        if (fread(gz->decompBuf, 1, toRead, disc->file) != toRead) return -1;
    } else {
        uint8_t* compBuf = (uint8_t*)malloc(compSize);
        if (!compBuf) return -1;
        if (fread(compBuf, 1, compSize, disc->file) != compSize) {
            free(compBuf);
            return -1;
        }

        uLongf destLen = gz->blockSize;
        int zret = uncompress(gz->decompBuf, &destLen, compBuf, (uLong)compSize);
        free(compBuf);
        if (zret != Z_OK) return -1;
    }

    gz->cachedBlock = blockIdx;
    return 0;
}

static int gcz_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    GCZData* gz = (GCZData*)disc->formatData;
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t blockIdx = offset / gz->blockSize;
        uint32_t blockOff = offset % gz->blockSize;
        size_t   chunk    = gz->blockSize - blockOff;
        if (chunk > remaining) chunk = remaining;

        if (blockIdx >= gz->numBlocks) {
            memset(out, 0, chunk);
        } else {
            if (gcz_decompress_block(disc, gz, blockIdx) < 0) return -1;
            memcpy(out, gz->decompBuf + blockOff, chunk);
        }

        out       += chunk;
        offset    += (uint32_t)chunk;
        remaining -= chunk;
    }

    return 0;
}

static void gcz_close(GCDisc* disc) {
    GCZData* gz = (GCZData*)disc->formatData;
    if (gz) {
        free(gz->blockPtrs);
        free(gz->decompBuf);
        free(gz);
    }
    disc->formatData = NULL;
}

int gc_gcz_open(GCDisc* disc) {
    uint8_t hdr[0x20];
    if (fseek(disc->file, 0, SEEK_SET) != 0) return -1;
    if (fread(hdr, 1, 0x20, disc->file) != 0x20) {
        siphon_log("GCZ header truncated");
        return -1;
    }

    GCZData* gz = (GCZData*)calloc(1, sizeof(GCZData));
    if (!gz) return -1;

    gz->blockSize   = gc_le32(hdr + 0x18);
    gz->numBlocks   = gc_le32(hdr + 0x1C);
    gz->dataSize    = gc_le64(hdr + 0x10);
    gz->cachedBlock = UINT32_MAX;

    if (gz->blockSize == 0 || gz->numBlocks == 0) {
        siphon_log("GCZ has invalid block_size=%u num_blocks=%u",
                gz->blockSize, gz->numBlocks);
        free(gz); return -1;
    }

    gz->blockPtrs = (uint64_t*)malloc(gz->numBlocks * sizeof(uint64_t));
    if (!gz->blockPtrs) { free(gz); return -1; }

    uint8_t* ptrBuf = (uint8_t*)malloc(gz->numBlocks * 8);
    if (!ptrBuf) { free(gz->blockPtrs); free(gz); return -1; }

    if (fread(ptrBuf, 1, gz->numBlocks * 8, disc->file) != gz->numBlocks * 8) {
        free(ptrBuf); free(gz->blockPtrs); free(gz); return -1;
    }

    uint64_t dataStart = 0x20 + (uint64_t)gz->numBlocks * 8 + (uint64_t)gz->numBlocks * 4;

    for (uint32_t i = 0; i < gz->numBlocks; i++) {
        uint64_t raw = gc_le64(ptrBuf + i * 8);
        uint64_t flag = raw & 0x8000000000000000ULL;
        uint64_t off  = (raw & 0x7FFFFFFFFFFFFFFFULL) + dataStart;
        gz->blockPtrs[i] = flag | off;
    }
    free(ptrBuf);

    gz->decompBuf = (uint8_t*)malloc(gz->blockSize);
    if (!gz->decompBuf) { free(gz->blockPtrs); free(gz); return -1; }

    disc->formatData = gz;
    disc->read  = gcz_read;
    disc->close = gcz_close;

    return gc_disc_parse_fst(disc);
}
