#include "wiiu_disc.h"
#include "gc_disc_internal.h"
#include "encrypt/platform.h"
#include "siphon_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WIIU_SECTOR_SIZE  0x8000u
#define WIIU_BLOCK_SIZE   0x10000u
#define WIIU_TOC_OFFSET   0x18000u
#define WIIU_TOC_ENTRY    0x80u
#define WIIU_TOC_NAME_LEN 0x19u

static const uint8_t WIIU_TOC_SIG[4] = {0xCC, 0xA6, 0xE6, 0x7B};
static const uint8_t WIIU_PART_SIG[4] = {0xCC, 0x93, 0xA4, 0xF5};

static uint64_t wiiu_le64(const uint8_t* p) {
    return (uint64_t)gc_le32(p) | ((uint64_t)gc_le32(p + 4) << 32);
}

static uint64_t wiiu_align_up(uint64_t v, uint64_t a) {
    if (a == 0) return v;
    return ((v + a - 1) / a) * a;
}

static void wiiu_trim(char* s) {
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    size_t len = strlen(start);
    while (len > 0) {
        char c = start[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { start[--len] = '\0'; } else break;
    }
    if (start != s) memmove(s, start, len + 1);
}

static int wiiu_hex16(const char* s, uint8_t out[16]) {
    if (strlen(s) != 32) return 0;
    for (int i = 0; i < 16; i++) {
        int hi = -1, lo = -1;
        char a = s[2 * i], b = s[2 * i + 1];
        if (a >= '0' && a <= '9') hi = a - '0';
        else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
        else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
        if (b >= '0' && b <= '9') lo = b - '0';
        else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
        else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static int wiiu_ci_contains(const char* hay, const char* needle) {
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen) {
            char h = p[i], n = needle[i];
            if (h >= 'A' && h <= 'Z') h = (char)(h - 'A' + 'a');
            if (n >= 'A' && n <= 'Z') n = (char)(n - 'A' + 'a');
            if (h != n) break;
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

typedef struct {
    FILE* f;
    uint64_t fileSize;
    int isWux;
    uint32_t sectorSize;
    uint64_t virtualSize;
    uint64_t dataOffset;
    uint32_t* map;
    uint64_t mapEntries;

    uint8_t* cache;
    int      cacheValid;
    uint64_t cacheOffset;
    uint8_t  cacheKey[16];
    uint8_t  cacheIv[16];
} WiiUImage;

static int wiiu_read_at(const WiiUImage* img, uint64_t offset, void* dst, size_t len) {
    if (fseeko(img->f, (off_t)offset, SEEK_SET) != 0) return -1;
    return fread(dst, 1, len, img->f) == len ? 0 : -1;
}

static int wiiu_read_virtual(const WiiUImage* img, uint64_t offset, void* dst, size_t len) {
    if (offset + len > img->virtualSize) return -1;
    if (!img->isWux) return wiiu_read_at(img, offset, dst, len);

    uint8_t* out = (uint8_t*)dst;
    while (len > 0) {
        uint64_t sector = offset / img->sectorSize;
        uint64_t intra = offset % img->sectorSize;
        uint64_t chunk = img->sectorSize - intra;
        if (chunk > len) chunk = len;
        if (sector >= img->mapEntries) return -1;
        uint32_t phys = img->map[sector];
        if (phys == 0xFFFFFFFFu) {
            memset(out, 0, (size_t)chunk);
        } else {
            uint64_t physOff = img->dataOffset + (uint64_t)phys * img->sectorSize + intra;
            if (wiiu_read_at(img, physOff, out, (size_t)chunk) != 0) return -1;
        }
        out += chunk;
        offset += chunk;
        len -= (size_t)chunk;
    }
    return 0;
}

static int wiiu_image_open(const char* path, WiiUImage* img) {
    memset(img, 0, sizeof(*img));
    img->f = gc_disc_fopen(path);
    if (!img->f) return -1;

    if (fseeko(img->f, 0, SEEK_END) != 0) { fclose(img->f); img->f = NULL; return -1; }
    img->fileSize = (uint64_t)ftello(img->f);

    uint8_t header[0x20];
    if (img->fileSize < sizeof(header) || wiiu_read_at(img, 0, header, sizeof(header)) != 0) {
        fclose(img->f); img->f = NULL; return -1;
    }

    if (memcmp(header, "WUX0", 4) == 0) {
        img->isWux = 1;
        img->sectorSize = gc_le32(header + 0x08);
        if (img->sectorSize == 0) img->sectorSize = WIIU_SECTOR_SIZE;
        img->virtualSize = wiiu_le64(header + 0x10);
        if (img->virtualSize == 0) { fclose(img->f); img->f = NULL; return -1; }

        uint64_t mapOffset = 0x20;
        img->mapEntries = (img->virtualSize + img->sectorSize - 1) / img->sectorSize;
        uint64_t mapBytes = img->mapEntries * 4;
        if (mapOffset + mapBytes > img->fileSize) { fclose(img->f); img->f = NULL; return -1; }

        img->map = (uint32_t*)malloc((size_t)mapBytes);
        if (!img->map) { fclose(img->f); img->f = NULL; return -1; }
        if (wiiu_read_at(img, mapOffset, img->map, (size_t)mapBytes) != 0) {
            free(img->map); img->map = NULL; fclose(img->f); img->f = NULL; return -1;
        }
        // Map is little-endian
        for (uint64_t i = 0; i < img->mapEntries; i++) {
            const uint8_t* p = (const uint8_t*)&img->map[i];
            img->map[i] = gc_le32(p);
        }
        img->dataOffset = wiiu_align_up(mapOffset + mapBytes, img->sectorSize);
    } else {
        img->isWux = 0;
        img->virtualSize = img->fileSize;
        img->sectorSize = WIIU_SECTOR_SIZE;
        img->dataOffset = 0;
    }
    return 0;
}

static void wiiu_image_close(WiiUImage* img) {
    if (img->cache) free(img->cache);
    if (img->map) free(img->map);
    if (img->f) fclose(img->f);
    memset(img, 0, sizeof(*img));
}

static const uint8_t* wiiu_get_block(WiiUImage* img, uint64_t blockOffset,
                                     const uint8_t key[16], const uint8_t iv[16]) {
    if (img->cacheValid && img->cacheOffset == blockOffset &&
        memcmp(img->cacheKey, key, 16) == 0 && memcmp(img->cacheIv, iv, 16) == 0) {
        return img->cache;
    }
    if (!img->cache) img->cache = (uint8_t*)malloc(WIIU_BLOCK_SIZE);
    if (!img->cache) return NULL;
    if (wiiu_read_virtual(img, blockOffset, img->cache, WIIU_BLOCK_SIZE) != 0) {
        img->cacheValid = 0;
        return NULL;
    }
    aes128_cbc_decrypt(key, iv, img->cache, img->cache, WIIU_BLOCK_SIZE);
    img->cacheValid = 1;
    img->cacheOffset = blockOffset;
    memcpy(img->cacheKey, key, 16);
    memcpy(img->cacheIv, iv, 16);
    return img->cache;
}

static int wiiu_read_decrypted(const WiiUImage* img, uint64_t clusterOffset, uint64_t fileOffset,
                               size_t size, const uint8_t key[16], const uint8_t* fixed_iv,
                               uint8_t* out) {
    WiiUImage* cacheOwner = (WiiUImage*)img; /* block cache lives on the image */
    size_t total = 0;
    uint64_t usedFile = fileOffset;
    size_t usedSize = size;
    while (total < size) {
        const uint64_t blockNumber = usedFile / WIIU_BLOCK_SIZE;
        const size_t blockOffset = (size_t)(usedFile % WIIU_BLOCK_SIZE);

        uint8_t iv[16];
        if (fixed_iv) {
            memcpy(iv, fixed_iv, 16);
        } else {
            memset(iv, 0, 8);
            uint64_t v = usedFile >> 16;
            for (int i = 0; i < 8; i++) iv[8 + i] = (uint8_t)((v >> (56 - 8 * i)) & 0xFF);
        }

        const uint8_t* block = wiiu_get_block(cacheOwner,
                                              clusterOffset + blockNumber * WIIU_BLOCK_SIZE, key, iv);
        if (!block) return -1;

        size_t copySize = usedSize < WIIU_BLOCK_SIZE - blockOffset
                              ? usedSize : WIIU_BLOCK_SIZE - blockOffset;
        memcpy(out + total, block + blockOffset, copySize);
        total += copySize;
        usedSize -= copySize;
        usedFile += copySize;
    }
    return 0;
}

/* Reads a content encrypted as ONE continuous AES-CBC stream. */
static int wiiu_read_content(const WiiUImage* img, uint64_t clusterOffset, size_t size,
                             const uint8_t key[16], const uint8_t iv[16], uint8_t* out) {
    size_t aligned = (size_t)wiiu_align_up(size, 16);
    if (wiiu_read_virtual(img, clusterOffset, out, aligned) != 0) return -1;
    aes128_cbc_decrypt(key, iv, out, out, aligned);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* FST                                                                        */
/* ------------------------------------------------------------------------- */

typedef struct WiiUNode {
    char* name;
    int isDir;
    uint64_t fileOffset;
    uint64_t fileSize;
    uint16_t contentIndex;
    struct WiiUNode* children;
    uint32_t childCount;
    uint32_t childCap;
} WiiUNode;

typedef struct {
    uint32_t offsetSector;
    uint32_t sizeSector;
} WiiUContent;

typedef struct {
    uint32_t sectorSize;
    uint32_t contentCount;
    uint32_t fileCount;
    WiiUContent* contents;
    WiiUNode root;
} WiiUFst;

// Cluster offsets in the FST table are in fixed 0x8000 disc sectors (Cemu uses
// DISC_SECTOR_SIZE), which is independent of the FST's own offsetFactor field
// (stored at FST+0x04 and used to scale *file* offsets).
static uint64_t wiiu_content_byte_offset(const WiiUContent* c, uint32_t sectorSize) {
    (void)sectorSize;
    int64_t v = (int64_t)c->offsetSector * (int64_t)WIIU_SECTOR_SIZE - (int64_t)WIIU_SECTOR_SIZE;
    return v < 0 ? 0 : (uint64_t)v;
}

static WiiUNode* wiiu_add_child(WiiUNode* parent, const WiiUNode* node) {
    if (parent->childCount == parent->childCap) {
        uint32_t cap = parent->childCap ? parent->childCap * 2 : 8;
        WiiUNode* n = (WiiUNode*)realloc(parent->children, (size_t)cap * sizeof(WiiUNode));
        if (!n) return NULL;
        parent->children = n;
        parent->childCap = cap;
    }
    parent->children[parent->childCount] = *node;
    return &parent->children[parent->childCount++];
}

static char* wiiu_read_name(const uint8_t* d, size_t dlen, size_t off) {
    size_t j = 0;
    if (off < dlen) {
        while (off + j < dlen && d[off + j] != 0) j++;
    }
    char* s = (char*)malloc(j + 1);
    if (!s) return NULL;
    if (j) memcpy(s, d + off, j);
    s[j] = '\0';
    return s;
}

static void wiiu_parse_entries(const uint8_t* d, size_t dlen, size_t fstBase, size_t nameBase,
                               uint32_t fileCount, uint32_t sectorSize,
                               uint32_t start, uint32_t end, WiiUNode* parent) {
    uint32_t i = start;
    while (i < end && i < fileCount) {
        if (fstBase + (size_t)i * 0x10 + 0x10 > dlen) break;
        const uint8_t* e = d + fstBase + (size_t)i * 0x10;
        uint8_t type = e[0];
        uint32_t nameOff = ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
        uint32_t rawOff = gc_be32(e + 0x04);
        uint32_t rawSize = gc_be32(e + 0x08);
        uint16_t ci = (uint16_t)((e[0x0E] << 8) | e[0x0F]);

        WiiUNode n;
        memset(&n, 0, sizeof(n));
        n.name = wiiu_read_name(d, dlen, nameBase + nameOff);
        n.contentIndex = ci;

        if (type & 0x01) {
            n.isDir = 1;
            n.fileSize = rawSize;
            WiiUNode* added = wiiu_add_child(parent, &n);
            if (!added) { free(n.name); return; }
            uint32_t dirEnd = rawSize;
            if (dirEnd > end) dirEnd = end;
            if (dirEnd < i + 1) dirEnd = i + 1;
            wiiu_parse_entries(d, dlen, fstBase, nameBase, fileCount, sectorSize, i + 1, dirEnd, added);
            i = dirEnd;
        } else {
            n.isDir = 0;
            n.fileOffset = (uint64_t)rawOff * sectorSize;
            n.fileSize = rawSize;
            if (!wiiu_add_child(parent, &n)) { free(n.name); return; }
            i++;
        }
    }
}

static void wiiu_node_free(WiiUNode* n) {
    for (uint32_t i = 0; i < n->childCount; i++) {
        wiiu_node_free(&n->children[i]);
    }
    free(n->children);
    free(n->name);
    n->children = NULL;
    n->name = NULL;
    n->childCount = n->childCap = 0;
}

static void wiiu_fst_free(WiiUFst* fst) {
    wiiu_node_free(&fst->root);
    free(fst->contents);
    memset(fst, 0, sizeof(*fst));
}

static int wiiu_parse_fst(const uint8_t* d, size_t dlen, WiiUFst* out) {
    memset(out, 0, sizeof(*out));
    if (dlen < 0x20 || d[0] != 0x46 || d[1] != 0x53 || d[2] != 0x54) return -1;

    out->sectorSize = gc_be32(d + 0x04);
    if (out->sectorSize == 0) out->sectorSize = WIIU_SECTOR_SIZE;
    out->contentCount = gc_be32(d + 0x08);

    size_t contentBase = 0x20;
    size_t fstBase = contentBase + (size_t)out->contentCount * 0x20;
    if (fstBase + 0x10 > dlen) return -1;

    uint32_t fileCount = gc_be32(d + fstBase + 0x08);
    out->fileCount = fileCount;
    size_t fstSize = (size_t)fileCount * 0x10;
    size_t nameBase = fstBase + fstSize;
    if (nameBase > dlen) nameBase = dlen;

    out->contents = (WiiUContent*)calloc(out->contentCount ? out->contentCount : 1, sizeof(WiiUContent));
    if (!out->contents) return -1;
    for (uint32_t i = 0; i < out->contentCount; i++) {
        const uint8_t* c = d + contentBase + (size_t)i * 0x20;
        out->contents[i].offsetSector = gc_be32(c + 0x00);
        out->contents[i].sizeSector = gc_be32(c + 0x04);
    }

    if (fstBase + fstSize <= dlen && fileCount > 0) {
        wiiu_parse_entries(d, dlen, fstBase, nameBase, fileCount, out->sectorSize, 1, fileCount, &out->root);
    }
    return 0;
}

typedef struct {
    uint8_t discKey[16];
    int hasDiscKey;
    uint8_t commonKey[16];
    int hasCommonKey;
    uint64_t titleId;
    uint8_t titleKey[16];
    int hasTitleKey;
    uint64_t gmContentOffset;
    WiiUFst fst;
} WiiUChain;

static int wiiu_load_keys(const char* keyspath, const WiiUImage* img, WiiUChain* ch) {
    FILE* f = fopen(keyspath, "rb");
    if (!f) return -1;

    uint8_t* toc = (uint8_t*)malloc(WIIU_SECTOR_SIZE);
    uint8_t* scratch = (uint8_t*)malloc(WIIU_SECTOR_SIZE);
    if (!toc || !scratch) {
        free(toc); free(scratch); fclose(f);
        return -1;
    }
    int haveToc = (wiiu_read_virtual(img, WIIU_TOC_OFFSET, toc, WIIU_SECTOR_SIZE) == 0);

    char line[512];
    int tried = 0;
    while (fgets(line, sizeof(line), f)) {
        char* hash = strchr(line, '#');
        char comment[256] = {0};
        if (hash) {
            snprintf(comment, sizeof(comment), "%s", hash + 1);
            *hash = '\0';
        }
        char hex[64];
        snprintf(hex, sizeof(hex), "%s", line);
        wiiu_trim(hex);
        wiiu_trim(comment);

        if (!ch->hasCommonKey && wiiu_ci_contains(comment, "Common")) {
            uint8_t k[16];
            if (wiiu_hex16(hex, k)) {
                memcpy(ch->commonKey, k, 16);
                ch->hasCommonKey = 1;
            }
            continue;
        }

        if (!ch->hasDiscKey && haveToc) {
            uint8_t k[16];
            if (!wiiu_hex16(hex, k)) continue;
            tried++;
            memcpy(scratch, toc, WIIU_SECTOR_SIZE);
            uint8_t iv[16] = {0};
            aes128_cbc_decrypt(k, iv, scratch, scratch, WIIU_SECTOR_SIZE);
            if (memcmp(scratch, WIIU_TOC_SIG, 4) == 0) {
                memcpy(ch->discKey, k, 16);
                ch->hasDiscKey = 1;
                siphon_log("WiiU: disc key matched after %d keys: %s (%s)", tried, hex, comment);
            }
        }
    }
    free(toc);
    free(scratch);
    fclose(f);
    if (!ch->hasDiscKey) {
        siphon_log("WiiU: no disc key matched (tried %d keys from %s)", tried, keyspath);
    }
    return ch->hasDiscKey ? 0 : -1;
}

typedef struct {
    char name[WIIU_TOC_NAME_LEN + 1];
    uint64_t offset;
} WiiUPart;

static int wiiu_parse_toc(const WiiUImage* img, const WiiUChain* ch,
                          WiiUPart* parts, uint32_t* outCount) {
    uint8_t* toc = (uint8_t*)malloc(WIIU_SECTOR_SIZE);
    if (!toc) return -1;

    int rc = -1;
    if (wiiu_read_virtual(img, WIIU_TOC_OFFSET, toc, WIIU_SECTOR_SIZE) != 0) goto done;
    uint8_t iv[16] = {0};
    aes128_cbc_decrypt(ch->discKey, iv, toc, toc, WIIU_SECTOR_SIZE);
    if (memcmp(toc, WIIU_TOC_SIG, 4) != 0) goto done;

    uint32_t count = gc_be32(toc + 0x1C);
    if (count > 16) count = 16;
    for (uint32_t i = 0; i < count; i++) {
        size_t off = 0x800 + (size_t)i * WIIU_TOC_ENTRY;
        memcpy(parts[i].name, toc + off, WIIU_TOC_NAME_LEN);
        parts[i].name[WIIU_TOC_NAME_LEN] = '\0';
        uint64_t sector = gc_be32(toc + off + 0x20);
        parts[i].offset = sector * WIIU_SECTOR_SIZE;
    }
    *outCount = count;
    rc = 0;
done:
    free(toc);
    return rc;
}

static const WiiUPart* wiiu_find_part(const WiiUPart* parts, uint32_t count, const char* prefix, size_t plen) {
    for (uint32_t i = 0; i < count; i++) {
        if (strncmp(parts[i].name, prefix, plen) == 0) return &parts[i];
    }
    return NULL;
}

static int wiiu_read_si_file(const WiiUImage* img, const WiiUChain* ch, const WiiUPart* si,
                             uint64_t headerSize, const WiiUFst* fst, const WiiUNode* dir,
                             const char* fname, uint8_t** outData, size_t* outSize) {
    const WiiUNode* file = NULL;
    for (uint32_t i = 0; i < dir->childCount; i++) {
        if (!dir->children[i].isDir && strcmp(dir->children[i].name, fname) == 0) {
            file = &dir->children[i];
            break;
        }
    }
    if (!file) return -1;
    if (file->contentIndex >= fst->contentCount) return -1;

    uint64_t base = si->offset + headerSize +
                    wiiu_content_byte_offset(&fst->contents[file->contentIndex], fst->sectorSize);
    uint8_t* buf = (uint8_t*)malloc((size_t)file->fileSize ? (size_t)file->fileSize : 1);
    if (!buf) return -1;
    if (wiiu_read_decrypted(img, base, file->fileOffset, (size_t)file->fileSize, ch->discKey, NULL, buf) != 0) {
        free(buf);
        return -1;
    }
    *outData = buf;
    *outSize = (size_t)file->fileSize;
    return 0;
}

static int wiiu_load_chain(const WiiUImage* img, const char* keyspath, WiiUChain* ch) {
    memset(ch, 0, sizeof(*ch));

    if (wiiu_load_keys(keyspath, img, ch) != 0) return -1;
    if (!ch->hasCommonKey) {
        siphon_log("WiiU: common key not found in %s", keyspath);
        return -1;
    }

    WiiUPart parts[16];
    uint32_t partCount = 0;
    if (wiiu_parse_toc(img, ch, parts, &partCount) != 0) return -1;

    const WiiUPart* si = wiiu_find_part(parts, partCount, "SI", 2);
    if (!si) { siphon_log("WiiU: no SI partition"); return -1; }

    uint8_t siHdr[0x20];
    if (wiiu_read_virtual(img, si->offset, siHdr, sizeof(siHdr)) != 0) return -1;
    if (memcmp(siHdr, WIIU_PART_SIG, 4) != 0) return -1;
    uint64_t siHeaderSize = gc_be32(siHdr + 0x04);
    uint32_t siFstSize = gc_be32(siHdr + 0x14);

    uint8_t* siFst = (uint8_t*)malloc(siFstSize);
    if (!siFst) return -1;
    uint8_t zeroIv[16] = {0};
    if (wiiu_read_decrypted(img, si->offset + siHeaderSize, 0, siFstSize, ch->discKey, zeroIv, siFst) != 0) {
        free(siFst);
        return -1;
    }
    WiiUFst siParsed;
    if (wiiu_parse_fst(siFst, siFstSize, &siParsed) != 0) {
        free(siFst);
        return -1;
    }
    free(siFst);

    for (uint32_t i = 0; i < siParsed.root.childCount; i++) {
        const WiiUNode* dir = &siParsed.root.children[i];
        if (!dir->isDir) continue;
        uint8_t* tik = NULL;
        size_t tikSize = 0;
        if (wiiu_read_si_file(img, ch, si, siHeaderSize, &siParsed, dir, "title.tik", &tik, &tikSize) != 0) {
            continue;
        }
        if (tikSize < 0x1E4) { free(tik); continue; }
        ch->titleId = gc_be64(tik + 0x1DC);
        uint8_t tiv[16] = {0};
        uint64_t tid = ch->titleId;
        for (int b = 0; b < 8; b++) tiv[b] = (uint8_t)((tid >> (56 - 8 * b)) & 0xFF);
        aes128_cbc_decrypt(ch->commonKey, tiv, tik + 0x1BF, ch->titleKey, 16);
        ch->hasTitleKey = 1;
        free(tik);
        break;
    }
    wiiu_fst_free(&siParsed);

    if (!ch->hasTitleKey) { siphon_log("WiiU: could not derive a title key"); return -1; }

    char gmPrefix[24];
    snprintf(gmPrefix, sizeof(gmPrefix), "GM%016llX", (unsigned long long)ch->titleId);
    const WiiUPart* gm = wiiu_find_part(parts, partCount, gmPrefix, strlen(gmPrefix));
    if (!gm) { siphon_log("WiiU: no GM partition matching %s", gmPrefix); return -1; }

    uint8_t gmHdr[0x20];
    if (wiiu_read_virtual(img, gm->offset, gmHdr, sizeof(gmHdr)) != 0) return -1;
    if (memcmp(gmHdr, WIIU_PART_SIG, 4) != 0) return -1;
    uint64_t gmHeaderSize = gc_be32(gmHdr + 0x04);
    ch->gmContentOffset = gm->offset + gmHeaderSize;

    uint8_t* head = (uint8_t*)malloc(WIIU_BLOCK_SIZE);
    if (!head) return -1;
    if (wiiu_read_content(img, ch->gmContentOffset, WIIU_BLOCK_SIZE, ch->titleKey, zeroIv, head) != 0) {
        free(head);
        return -1;
    }
    if (head[0] != 0x46 || head[1] != 0x53 || head[2] != 0x54) {
        free(head);
        siphon_log("WiiU: GM FST magic mismatch");
        return -1;
    }
    uint32_t contentCount = gc_be32(head + 0x08);
    size_t fstBase = 0x20 + (size_t)contentCount * 0x20;
    uint32_t fileCount = (fstBase + 0x10 <= WIIU_BLOCK_SIZE) ? gc_be32(head + fstBase + 0x08) : 0;
    free(head);

    size_t want = fstBase + (size_t)fileCount * 0x10 + 0x80000;
    if (want < WIIU_BLOCK_SIZE) want = WIIU_BLOCK_SIZE;
    if (want > 8u * 1024u * 1024u) want = 8u * 1024u * 1024u;

    uint8_t* fstBytes = (uint8_t*)malloc((size_t)wiiu_align_up(want, 16));
    if (!fstBytes) return -1;
    if (wiiu_read_content(img, ch->gmContentOffset, want, ch->titleKey, zeroIv, fstBytes) != 0) {
        free(fstBytes);
        return -1;
    }
    if (wiiu_parse_fst(fstBytes, (size_t)wiiu_align_up(want, 16), &ch->fst) != 0) {
        free(fstBytes);
        siphon_log("WiiU: GM FST parse failed");
        return -1;
    }
    free(fstBytes);
    siphon_log("WiiU GM FST OK: %u content(s), %u file(s)", ch->fst.contentCount, ch->fst.fileCount);
    return 0;
}

static void wiiu_chain_free(WiiUChain* ch) {
    wiiu_fst_free(&ch->fst);
}

typedef struct {
    const WiiUImage* img;
    const WiiUChain* ch;
    size_t files;
    uint64_t bytes;
    int failures;
} WiiUExtractCtx;

static int wiiu_extract_file(WiiUExtractCtx* ctx, const WiiUNode* n, const char* outPath) {
    if (n->contentIndex >= ctx->ch->fst.contentCount) return -1;
    const uint64_t contentOffset = ctx->ch->gmContentOffset +
                             wiiu_content_byte_offset(&ctx->ch->fst.contents[n->contentIndex],
                                                      ctx->ch->fst.sectorSize);

    uint8_t* buf = (uint8_t*)malloc(n->fileSize ? (size_t)n->fileSize : 1);
    if (!buf) return -1;

    if (wiiu_read_decrypted(ctx->img, contentOffset, n->fileOffset, (size_t)n->fileSize,
                            ctx->ch->titleKey, NULL, buf) != 0) {
        free(buf);
        return -1;
    }

    int rc = gc_disc_write_file(outPath, buf, (size_t)n->fileSize);
    free(buf);
    return rc;
}

static void wiiu_extract_node(WiiUExtractCtx* ctx, const WiiUNode* n, const char* parentPath) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", parentPath, n->name);
    if (n->isDir) {
        gc_disc_mkdirs(path);
        for (uint32_t i = 0; i < n->childCount; i++) {
            wiiu_extract_node(ctx, &n->children[i], path);
        }
        return;
    }
    if (wiiu_extract_file(ctx, n, path) != 0) {
        ctx->failures++;
        if (ctx->failures <= 10) siphon_log("WiiU: failed to extract '%s'", path);
        return;
    }
    ctx->files++;
    ctx->bytes += n->fileSize;
    if ((ctx->files % 500) == 0) {
        siphon_log("WiiU: extracted %zu file(s), %llu bytes...", ctx->files,
                   (unsigned long long)ctx->bytes);
    }
}

int wiiu_disc_is_wiiu(const char* path) {
    if (!path) return 0;
    FILE* f = gc_disc_fopen(path);
    if (!f) return 0;
    uint8_t magic[4] = {0};
    size_t n = fread(magic, 1, sizeof(magic), f);
    fclose(f);
    if (n == 4 && memcmp(magic, "WUX0", 4) == 0) return 1;

    const char* dot = strrchr(path, '.');
    if (dot) {
        if (wiiu_ci_contains(dot, "wux") || wiiu_ci_contains(dot, "wud")) return 1;
    }
    return 0;
}

int wiiu_disc_inspect(const char* path, char* game_id, size_t game_id_sz,
                      char* format, size_t format_sz) {
    WiiUImage img;
    if (wiiu_image_open(path, &img) != 0) return -1;

    if (format && format_sz) snprintf(format, format_sz, "%s", img.isWux ? "WUX" : "WUD");

    if (game_id && game_id_sz) {
        uint8_t hdr[0x20] = {0};
        if (wiiu_read_virtual(&img, 0, hdr, sizeof(hdr)) == 0) {
            hdr[sizeof(hdr) - 1] = '\0';
            const char* code = (const char*)hdr;
            if (strncmp(code, "WUP", 3) == 0 && strlen(code) >= 12) {
                snprintf(game_id, game_id_sz, "%.6s", code + 6);
            } else {
                snprintf(game_id, game_id_sz, "%.6s", code);
            }
        }
    }

    wiiu_image_close(&img);
    return 0;
}

int wiiu_disc_extract(const char* path, const char* outdir, const char* keys_path) {
    if (!keys_path || !keys_path[0]) {
        siphon_log("WiiU: a keys.txt path is required");
        return -1;
    }

    WiiUImage img;
    if (wiiu_image_open(path, &img) != 0) {
        siphon_log("WiiU: failed to open %s", path);
        return -1;
    }
    siphon_log("WiiU: using key cache %s", keys_path);

    WiiUChain ch;
    if (wiiu_load_chain(&img, keys_path, &ch) != 0) {
        siphon_log("WiiU: failed to load the game partition");
        wiiu_image_close(&img);
        return -1;
    }

    gc_disc_mkdirs(outdir);
    WiiUExtractCtx ctx;
    ctx.img = &img;
    ctx.ch = &ch;
    ctx.files = 0;
    ctx.bytes = 0;
    ctx.failures = 0;

    for (uint32_t i = 0; i < ch.fst.root.childCount; i++) {
        wiiu_extract_node(&ctx, &ch.fst.root.children[i], outdir);
    }

    siphon_log("WiiU: extracted %zu file(s), %llu bytes to %s", ctx.files,
               (unsigned long long)ctx.bytes, outdir);

    wiiu_chain_free(&ch);
    wiiu_image_close(&img);
    return ctx.failures ? -1 : 0;
}
