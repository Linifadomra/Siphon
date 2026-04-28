#include "gc_disc_internal.h"
#include "siphon_log.h"
#include "gc_rvz.h"
#include <stdlib.h>
#include <string.h>

#define WIA_COMP_NONE  0
#define WIA_COMP_PURGE 1
#define WIA_COMP_BZIP2 2
#define WIA_COMP_LZMA  3
#define WIA_COMP_LZMA2 4
#define WIA_COMP_ZSTD  5

#if __has_include(<zstd.h>)
#include <zstd.h>
#define HAS_ZSTD 1
#else
#define HAS_ZSTD 0
#endif

typedef struct {
    uint64_t dataOffset;
    uint64_t dataSize;
    uint32_t groupIndex;
    uint32_t numGroups;
} WIARawData;

typedef struct {
    uint32_t fileOffset;
    uint32_t dataSize;     // RVZ: bit 31 = compressed flag
    uint32_t rvzPackedSize;
} WIAGroup;

typedef struct {
    int       isRVZ;
    uint32_t  compType;
    uint32_t  chunkSize;
    uint8_t   discHeader[0x80];

    uint32_t  numRawData;
    WIARawData* rawData;

    uint32_t  numGroups;
    WIAGroup* groups;

    uint8_t*  decompBuf;
    size_t    decompBufCap;
    uint32_t  decompBufSize;
    uint32_t  cachedGroupIdx;

    uint8_t*  packedScratch;
    size_t    packedScratchCap;
} WIAData;

static int wia_decompress_buf(uint32_t compType, const uint8_t* src, size_t srcLen,
                              uint8_t* dst, size_t dstCap, size_t* outLen) {
    switch (compType) {
        case WIA_COMP_NONE:
            if (srcLen > dstCap) return -1;
            memcpy(dst, src, srcLen);
            *outLen = srcLen;
            return 0;

#if HAS_ZSTD
        case WIA_COMP_ZSTD: {
            size_t ret = ZSTD_decompress(dst, dstCap, src, srcLen);
            if (ZSTD_isError(ret)) return -1;
            *outLen = ret;
            return 0;
        }
#endif
        default:
            return -1;
    }
}

static int wia_read_and_decompress(FILE* f, uint32_t compType,
                                   uint64_t fileOff, uint32_t compSize,
                                   uint8_t* dst, size_t dstCap, size_t* outLen) {
    if (compType == WIA_COMP_NONE) {
        if (fseek(f, (long)fileOff, SEEK_SET) != 0) return -1;
        size_t toRead = compSize < dstCap ? compSize : dstCap;
        if (fread(dst, 1, toRead, f) != toRead) return -1;
        *outLen = toRead;
        return 0;
    }

    uint8_t* compBuf = (uint8_t*)malloc(compSize);
    if (!compBuf) return -1;
    if (fseek(f, (long)fileOff, SEEK_SET) != 0) { free(compBuf); return -1; }
    if (fread(compBuf, 1, compSize, f) != compSize) { free(compBuf); return -1; }

    int ret = wia_decompress_buf(compType, compBuf, compSize, dst, dstCap, outLen);
    free(compBuf);
    return ret;
}

static int wia_decompress_group(GCDisc* disc, WIAData* wd, uint32_t groupIdx,
                                uint64_t data_offset) {
    if (wd->cachedGroupIdx == groupIdx) return 0;

    WIAGroup* grp = &wd->groups[groupIdx];

    uint32_t compSize = grp->dataSize;
    int isCompressed = 1;

    if (wd->isRVZ) {
        isCompressed = (compSize >> 31) & 1;
        compSize &= 0x7FFFFFFF;
    }

    if (compSize == 0) {
        memset(wd->decompBuf, 0, wd->chunkSize);
        wd->decompBufSize = wd->chunkSize;
        wd->cachedGroupIdx = groupIdx;
        return 0;
    }

    uint32_t effCompType = isCompressed ? wd->compType : WIA_COMP_NONE;

    size_t outLen = 0;
    if (wia_read_and_decompress(disc->file, effCompType,
                                grp->fileOffset, compSize,
                                wd->decompBuf, wd->decompBufCap, &outLen) < 0) {
        return -1;
    }

    if (wd->isRVZ && grp->rvzPackedSize != 0 && isCompressed) {
        if (outLen > wd->packedScratchCap) {
            uint8_t* grown = (uint8_t*)realloc(wd->packedScratch, outLen);
            if (!grown) return -1;
            wd->packedScratch = grown;
            wd->packedScratchCap = outLen;
        }
        memcpy(wd->packedScratch, wd->decompBuf, outLen);

        size_t unpacked = 0;
        if (gc_rvz_unpack(wd->packedScratch, outLen, data_offset,
                       wd->decompBuf, wd->decompBufCap, &unpacked) != 0) {
            return -1;
        }
        outLen = unpacked;
    }

    wd->decompBufSize = (uint32_t)outLen;
    wd->cachedGroupIdx = groupIdx;
    return 0;
}

static int wia_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    WIAData* wd = (WIAData*)disc->formatData;
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = size;

    while (remaining > 0) {
        if (offset < 0x80) {
            size_t chunk = 0x80 - offset;
            if (chunk > remaining) chunk = remaining;
            memcpy(out, wd->discHeader + offset, chunk);
            out += chunk;
            offset += (uint32_t)chunk;
            remaining -= chunk;
            continue;
        }

        int found = 0;
        for (uint32_t r = 0; r < wd->numRawData; r++) {
            WIARawData* rd = &wd->rawData[r];

            uint64_t origOffset = rd->dataOffset;
            uint64_t origSize   = rd->dataSize;
            uint64_t skipped    = origOffset % 0x8000;
            uint64_t adjOffset  = origOffset - skipped;
            uint64_t adjSize    = origSize + skipped;

            if (offset < adjOffset || offset >= adjOffset + adjSize) continue;

            found = 1;
            uint64_t localOff = offset - adjOffset;

            while (remaining > 0 && localOff < adjSize) {
                uint32_t groupRel = (uint32_t)(localOff / wd->chunkSize);
                uint32_t inGroup  = (uint32_t)(localOff % wd->chunkSize);
                uint32_t groupIdx = rd->groupIndex + groupRel;

                if (groupIdx >= wd->numGroups) {
                    memset(out, 0, remaining);
                    return 0;
                }

                size_t chunk = wd->chunkSize - inGroup;
                if (chunk > remaining) chunk = remaining;
                if (chunk > adjSize - localOff) chunk = (size_t)(adjSize - localOff);

                uint64_t groupDiscOffset = adjOffset + (uint64_t)groupRel * wd->chunkSize;
                if (wia_decompress_group(disc, wd, groupIdx, groupDiscOffset) < 0) return -1;

                if (inGroup + chunk > wd->decompBufSize) {
                    size_t valid = wd->decompBufSize > inGroup ? wd->decompBufSize - inGroup : 0;
                    if (valid > 0) memcpy(out, wd->decompBuf + inGroup, valid);
                    if (chunk > valid) memset(out + valid, 0, chunk - valid);
                } else {
                    memcpy(out, wd->decompBuf + inGroup, chunk);
                }

                out      += chunk;
                offset   += (uint32_t)chunk;
                localOff += chunk;
                remaining -= chunk;
            }
            break;
        }

        if (!found) {
            memset(out, 0, remaining);
            out += remaining;
            offset += (uint32_t)remaining;
            remaining = 0;
        }
    }

    return 0;
}

static void wia_close(GCDisc* disc) {
    WIAData* wd = (WIAData*)disc->formatData;
    if (wd) {
        free(wd->rawData);
        free(wd->groups);
        free(wd->decompBuf);
        free(wd->packedScratch);
        free(wd);
    }
    disc->formatData = NULL;
}

int gc_wia_open(GCDisc* disc, int isRVZ) {
    const char* kind = isRVZ ? "RVZ" : "WIA";
    uint8_t hdrs[0x48 + 0xDC];
    if (fseek(disc->file, 0, SEEK_SET) != 0) return -1;
    if (fread(hdrs, 1, sizeof(hdrs), disc->file) != sizeof(hdrs)) {
        siphon_log("%s header truncated", kind);
        return -1;
    }

    const uint8_t* h2 = hdrs + 0x48;

    WIAData* wd = (WIAData*)calloc(1, sizeof(WIAData));
    if (!wd) return -1;

    wd->isRVZ    = isRVZ;
    wd->compType = gc_be32(h2 + 0x04);
    wd->chunkSize = gc_be32(h2 + 0x0C);
    wd->cachedGroupIdx = UINT32_MAX;

    if (wd->chunkSize == 0) {
        siphon_log("%s chunk size is zero", kind);
        free(wd); return -1;
    }

    memcpy(wd->discHeader, h2 + 0x10, 0x80);

    if (wd->compType != WIA_COMP_NONE) {
#if !HAS_ZSTD
        if (wd->compType == WIA_COMP_ZSTD) { free(wd); return -1; }
#endif
        if (wd->compType == WIA_COMP_BZIP2 || wd->compType == WIA_COMP_LZMA ||
            wd->compType == WIA_COMP_LZMA2 || wd->compType == WIA_COMP_PURGE) {
            const char* name = "unknown";
            switch (wd->compType) {
                case WIA_COMP_BZIP2: name = "bzip2"; break;
                case WIA_COMP_LZMA:  name = "lzma";  break;
                case WIA_COMP_LZMA2: name = "lzma2"; break;
                case WIA_COMP_PURGE: name = "purge"; break;
            }
            fprintf(stderr,
                "siphon: %s compression is not supported in this build "
                "(only NONE and ZSTD are implemented). Re-encode the image to "
                "zstd with Dolphin if you need to extract it.",
                name);
            free(wd);
            return -1;
        }
    }

    wd->numRawData = gc_be32(h2 + 0xB4);
    uint64_t rawOff  = gc_be64(h2 + 0xB8);
    uint32_t rawSize = gc_be32(h2 + 0xC0);

    if (wd->numRawData == 0) { free(wd); return -1; }

    uint32_t rawEntrySize = 0x18;
    size_t rawBufSize = (size_t)wd->numRawData * rawEntrySize + 4096;
    uint8_t* rawBuf = (uint8_t*)malloc(rawBufSize);
    if (!rawBuf) { free(wd); return -1; }

    size_t rawOutLen = 0;
    if (wia_read_and_decompress(disc->file, wd->compType, rawOff, rawSize,
                                rawBuf, rawBufSize, &rawOutLen) < 0) {
        free(rawBuf); free(wd); return -1;
    }

    wd->rawData = (WIARawData*)calloc(wd->numRawData, sizeof(WIARawData));
    if (!wd->rawData) { free(rawBuf); free(wd); return -1; }

    for (uint32_t i = 0; i < wd->numRawData; i++) {
        const uint8_t* e = rawBuf + i * rawEntrySize;
        wd->rawData[i].dataOffset = gc_be64(e + 0x00);
        wd->rawData[i].dataSize   = gc_be64(e + 0x08);
        wd->rawData[i].groupIndex = gc_be32(e + 0x10);
        wd->rawData[i].numGroups  = gc_be32(e + 0x14);
    }
    free(rawBuf);

    wd->numGroups = gc_be32(h2 + 0xC4);
    uint64_t grpOff  = gc_be64(h2 + 0xC8);
    uint32_t grpSize = gc_be32(h2 + 0xD0);

    uint32_t grpEntrySize = isRVZ ? 0x0C : 0x08;
    size_t grpBufSize = (size_t)wd->numGroups * grpEntrySize + 4096;
    uint8_t* grpBuf = (uint8_t*)malloc(grpBufSize);
    if (!grpBuf) { wia_close(disc); return -1; }

    size_t grpOutLen = 0;
    if (wia_read_and_decompress(disc->file, wd->compType, grpOff, grpSize,
                                grpBuf, grpBufSize, &grpOutLen) < 0) {
        free(grpBuf); wia_close(disc); return -1;
    }

    wd->groups = (WIAGroup*)calloc(wd->numGroups, sizeof(WIAGroup));
    if (!wd->groups) { free(grpBuf); wia_close(disc); return -1; }

    for (uint32_t i = 0; i < wd->numGroups; i++) {
        const uint8_t* e = grpBuf + i * grpEntrySize;
        wd->groups[i].fileOffset = gc_be32(e + 0) << 2;
        wd->groups[i].dataSize   = gc_be32(e + 4);
        if (isRVZ && grpEntrySize >= 0x0C) {
            wd->groups[i].rvzPackedSize = gc_be32(e + 8);
        }
    }
    free(grpBuf);

    wd->decompBufCap = wd->chunkSize + 4096;
    wd->decompBuf = (uint8_t*)malloc(wd->decompBufCap);
    if (!wd->decompBuf) { wia_close(disc); return -1; }

    disc->formatData = wd;
    disc->read  = wia_read;
    disc->close = wia_close;

    return gc_disc_parse_fst(disc);
}
