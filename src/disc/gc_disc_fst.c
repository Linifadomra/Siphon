#include "gc_disc_internal.h"
#include "siphon_log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "macros.h"

static void mkdirs(const char* path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
#ifdef _WIN32
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            MKDIR_ONE(tmp);
            *p = '/';
        }
    }
    MKDIR_ONE(tmp);
#else
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            MKDIR_ONE(tmp);
            *p = '/';
        }
    }
    MKDIR_ONE(tmp);
#endif
}

int gc_disc_parse_fst(GCDisc* disc) {
    if (disc->read(disc, 0, disc->boot, 0x440) < 0) return -1;

    memcpy(disc->gameId, disc->boot, 6);
    disc->gameId[6] = '\0';

    disc->dolOffset = gc_be32(disc->boot + 0x420);
    disc->fstOffset = gc_be32(disc->boot + 0x424);
    disc->fstSize   = gc_be32(disc->boot + 0x428);

    if (disc->read(disc, 0x440, disc->bi2, 0x2000) < 0) return -1;

    uint8_t appHdr[0x20];
    if (disc->read(disc, 0x2440, appHdr, 0x20) < 0) return -1;
    uint32_t appCode    = gc_be32(appHdr + 0x14);
    uint32_t appTrailer = gc_be32(appHdr + 0x18);
    disc->apploaderSize = 0x20 + appCode + appTrailer;
    disc->apploader = (uint8_t*)malloc(disc->apploaderSize);
    if (!disc->apploader) return -1;
    if (disc->read(disc, 0x2440, disc->apploader, disc->apploaderSize) < 0) return -1;

    disc->fstData = (uint8_t*)malloc(disc->fstSize);
    if (!disc->fstData) return -1;
    if (disc->read(disc, disc->fstOffset, disc->fstData, disc->fstSize) < 0) return -1;

    disc->entryCount = gc_be32(disc->fstData + 8);
    if (disc->entryCount == 0 || disc->entryCount > 100000) return -1;

    size_t strOff = disc->entryCount * 12;
    disc->stringTable = (const char*)(disc->fstData + strOff);

    disc->entries = (GCEntry*)calloc(disc->entryCount, sizeof(GCEntry));
    if (!disc->entries) return -1;

    size_t totalPathLen = 0;
    const char* dirPaths[256];
    size_t dirLens[256];
    uint32_t dirEnd[256];
    int dirDepth = 1;

    dirEnd[0] = disc->entryCount;
    dirLens[0] = 0;
    dirPaths[0] = "";

    for (uint32_t i = 1; i < disc->entryCount; i++) {
        const uint8_t* e = disc->fstData + i * 12;
        uint32_t nameOff = ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
        const char* name = disc->stringTable + nameOff;
        size_t nameLen = strlen(name);

        while (dirDepth > 1 && i >= dirEnd[dirDepth - 1]) dirDepth--;

        totalPathLen += dirLens[dirDepth - 1] + 1 + nameLen + 1;

        if (e[0]) {
            if (dirDepth < 256) {
                dirEnd[dirDepth] = gc_be32(e + 8);
                dirLens[dirDepth] = dirLens[dirDepth - 1] + 1 + nameLen;
                dirDepth++;
            } else {
                siphon_log("FST directory nesting exceeds 256; aborting");
                return -1;
            }
        }
    }

    disc->pathBuf = (char*)malloc(totalPathLen);
    if (!disc->pathBuf) return -1;

    char* pathWrite = disc->pathBuf;
    dirDepth = 1;
    dirEnd[0] = disc->entryCount;
    dirLens[0] = 0;
    dirPaths[0] = "";

    disc->entries[0].type = GC_ENTRY_DIR;
    disc->entries[0].name = "";
    disc->entries[0].discOffset = 0;
    disc->entries[0].size = disc->entryCount;

    for (uint32_t i = 1; i < disc->entryCount; i++) {
        const uint8_t* e = disc->fstData + i * 12;
        uint32_t nameOff = ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
        const char* name = disc->stringTable + nameOff;
        size_t nameLen = strlen(name);

        while (dirDepth > 1 && i >= dirEnd[dirDepth - 1]) dirDepth--;

        char* pathStart = pathWrite;
        if (dirLens[dirDepth - 1] > 0) {
            memcpy(pathWrite, dirPaths[dirDepth - 1], dirLens[dirDepth - 1]);
            pathWrite += dirLens[dirDepth - 1];
            *pathWrite++ = '/';
        }
        memcpy(pathWrite, name, nameLen);
        pathWrite += nameLen;
        *pathWrite++ = '\0';

        disc->entries[i].name = pathStart;
        disc->entries[i].discOffset = gc_be32(e + 4);
        disc->entries[i].size = gc_be32(e + 8);

        if (e[0]) {
            disc->entries[i].type = GC_ENTRY_DIR;
            if (dirDepth < 256) {
                dirEnd[dirDepth] = disc->entries[i].size;
                dirPaths[dirDepth] = pathStart;
                dirLens[dirDepth] = (size_t)(pathWrite - 1 - pathStart);
                dirDepth++;
            }
        } else {
            disc->entries[i].type = GC_ENTRY_FILE;
        }
    }

    return 0;
}

void gc_disc_free_parsed(GCDisc* disc) {
    if (!disc) return;
    free(disc->apploader);  disc->apploader = NULL;
    free(disc->fstData);    disc->fstData = NULL;
    free(disc->entries);    disc->entries = NULL;
    free(disc->pathBuf);    disc->pathBuf = NULL;
}

static int write_buf(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    int ok = (fwrite(data, 1, size, f) == size) ? 0 : -1;
    fclose(f);
    return ok;
}

static size_t calc_dol_size(const uint8_t* hdr) {
    size_t max = 0x100;
    for (int i = 0; i < 18; i++) {
        uint32_t off = gc_be32(hdr + i * 4);
        uint32_t sz  = gc_be32(hdr + 0x90 + i * 4);
        if (off && sz) {
            size_t end = (size_t)off + sz;
            if (end > max) max = end;
        }
    }
    return max;
}

int gc_disc_extract_file(GCDisc* disc, int index, const char* outputPath) {
    if (!disc || index < 0 || (uint32_t)index >= disc->entryCount) return -1;
    const GCEntry* e = &disc->entries[index];
    if (e->type != GC_ENTRY_FILE) return -1;

    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", outputPath);
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdirs(dir);
    }

    static const size_t BUF_SIZE = 1 * 1024 * 1024;
    uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) return -1;

    int ret = 0;

    if (e->size <= BUF_SIZE) {
        if (disc->read(disc, e->discOffset, buf, e->size) < 0 ||
            write_buf(outputPath, buf, e->size) < 0) {
            ret = -1;
        }
    } else {
        FILE* out = fopen(outputPath, "wb");
        if (!out) { free(buf); return -1; }
        size_t remaining = e->size;
        uint32_t off = e->discOffset;
        while (remaining > 0) {
            size_t chunk = remaining < BUF_SIZE ? remaining : BUF_SIZE;
            if (disc->read(disc, off, buf, chunk) < 0 ||
                fwrite(buf, 1, chunk, out) != chunk) {
                ret = -1;
                break;
            }
            off += (uint32_t)chunk;
            remaining -= chunk;
        }
        fclose(out);
    }

    free(buf);
    return ret;
}

static int cmp_by_offset(const void* a, const void* b) {
    uint32_t oa = ((const GCEntry*)a)->discOffset;
    uint32_t ob = ((const GCEntry*)b)->discOffset;
    if (oa < ob) return -1;
    if (oa > ob) return 1;
    return 0;
}

int gc_disc_extract_all(GCDisc* disc, const char* outputDir) {
    if (!disc) return -1;

    char sysDir[4096], filesDir[4096];
    snprintf(sysDir, sizeof(sysDir), "%s/sys", outputDir);
    snprintf(filesDir, sizeof(filesDir), "%s/files", outputDir);
    mkdirs(sysDir);
    mkdirs(filesDir);

    char path[4096];
    snprintf(path, sizeof(path), "%s/boot.bin", sysDir);
    if (write_buf(path, disc->boot, 0x440) < 0) return -1;

    snprintf(path, sizeof(path), "%s/bi2.bin", sysDir);
    if (write_buf(path, disc->bi2, 0x2000) < 0) return -1;

    snprintf(path, sizeof(path), "%s/apploader.img", sysDir);
    if (write_buf(path, disc->apploader, disc->apploaderSize) < 0) return -1;

    snprintf(path, sizeof(path), "%s/fst.bin", sysDir);
    if (write_buf(path, disc->fstData, disc->fstSize) < 0) return -1;

    uint8_t dolHdr[0x100];
    if (disc->read(disc, disc->dolOffset, dolHdr, 0x100) < 0) return -1;
    size_t dolSize = calc_dol_size(dolHdr);

    static const size_t BUF_SIZE = 1 * 1024 * 1024;
    uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) return -1;

    snprintf(path, sizeof(path), "%s/main.dol", sysDir);
    {
        FILE* out = fopen(path, "wb");
        if (!out) { free(buf); return -1; }
        size_t remaining = dolSize;
        uint32_t off = disc->dolOffset;
        while (remaining > 0) {
            size_t chunk = remaining < BUF_SIZE ? remaining : BUF_SIZE;
            if (disc->read(disc, off, buf, chunk) < 0 ||
                fwrite(buf, 1, chunk, out) != chunk) {
                fclose(out); free(buf); return -1;
            }
            off += (uint32_t)chunk;
            remaining -= chunk;
        }
        fclose(out);
    }

    int fileCount = 0;
    for (uint32_t i = 1; i < disc->entryCount; i++) {
        if (disc->entries[i].type == GC_ENTRY_FILE) fileCount++;
    }

    GCEntry* sorted = (GCEntry*)malloc(fileCount * sizeof(GCEntry));
    if (!sorted) { free(buf); return -1; }

    int si = 0;
    for (uint32_t i = 1; i < disc->entryCount; i++) {
        if (disc->entries[i].type == GC_ENTRY_FILE)
            sorted[si++] = disc->entries[i];
    }

    qsort(sorted, fileCount, sizeof(GCEntry), cmp_by_offset);

    char lastDir[4096] = {0};

    int ret = 0;
    for (int i = 0; i < fileCount; i++) {
        const GCEntry* e = &sorted[i];
        if (e->size == 0) continue;

        snprintf(path, sizeof(path), "%s/%s", filesDir, e->name);

        char* slash = strrchr(path, '/');
        if (slash) {
            size_t dirLen = (size_t)(slash - path);
            if (dirLen >= sizeof(lastDir) || strncmp(path, lastDir, dirLen) != 0 || lastDir[dirLen] != '\0') {
                char tmp = *slash;
                *slash = '\0';
                mkdirs(path);
                snprintf(lastDir, sizeof(lastDir), "%s", path);
                *slash = tmp;
            }
        }

        if (e->size <= BUF_SIZE) {
            if (disc->read(disc, e->discOffset, buf, e->size) < 0 ||
                write_buf(path, buf, e->size) < 0) {
                siphon_log("extract failed at %s (off=0x%X sz=%u)",
                        e->name, e->discOffset, e->size);
                ret = -1;
                break;
            }
        } else {
            FILE* out = fopen(path, "wb");
            if (!out) {
                siphon_log("fopen failed: %s", path);
                ret = -1;
                break;
            }
            size_t remaining = e->size;
            uint32_t off = e->discOffset;
            while (remaining > 0) {
                size_t chunk = remaining < BUF_SIZE ? remaining : BUF_SIZE;
                if (disc->read(disc, off, buf, chunk) < 0 ||
                    fwrite(buf, 1, chunk, out) != chunk) {
                    siphon_log("read/write failed: %s", e->name);
                    fclose(out); ret = -1;
                    goto done;
                }
                off += (uint32_t)chunk;
                remaining -= chunk;
            }
            fclose(out);
        }
    }
done:

    free(sorted);
    free(buf);
    return ret;
}
