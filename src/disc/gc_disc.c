#include "gc_disc_internal.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t MAGIC_CISO[4] = {'C','I','S','O'};
static const uint8_t MAGIC_WIA[4]  = {'W','I','A',0x01};
static const uint8_t MAGIC_RVZ[4]  = {'R','V','Z',0x01};
static const uint8_t MAGIC_GCZ[4]  = {0x01,0xC0,0x0B,0xB1};
static const uint8_t MAGIC_WBFS[4] = {'W','B','F','S'};

GCDiscFormat gc_disc_detect_format(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return GC_FORMAT_UNKNOWN;

    uint8_t hdr[4];
    size_t rd = fread(hdr, 1, 4, f);
    fclose(f);
    if (rd < 4) return GC_FORMAT_UNKNOWN;

    if (memcmp(hdr, MAGIC_CISO, 4) == 0) return GC_FORMAT_CISO;
    if (memcmp(hdr, MAGIC_WIA,  4) == 0) return GC_FORMAT_WIA;
    if (memcmp(hdr, MAGIC_RVZ,  4) == 0) return GC_FORMAT_RVZ;
    if (memcmp(hdr, MAGIC_GCZ,  4) == 0) return GC_FORMAT_GCZ;
    if (memcmp(hdr, MAGIC_WBFS, 4) == 0) return GC_FORMAT_WBFS;

    // Raw ISO game IDs start with an uppercase letter.
    if (hdr[0] >= 'A' && hdr[0] <= 'Z') return GC_FORMAT_ISO;

    return GC_FORMAT_UNKNOWN;
}

GCDisc* gc_disc_open(const char* path) {
    GCDiscFormat fmt = gc_disc_detect_format(path);
    if (fmt == GC_FORMAT_UNKNOWN) return NULL;

    GCDisc* disc = (GCDisc*)calloc(1, sizeof(GCDisc));
    if (!disc) return NULL;

    disc->file = fopen(path, "rb");
    if (!disc->file) {
        free(disc);
        return NULL;
    }

    disc->format = fmt;
    int err = -1;

    switch (fmt) {
        case GC_FORMAT_ISO:  err = gc_iso_open(disc);      break;
        case GC_FORMAT_CISO: err = gc_ciso_open(disc);     break;
        case GC_FORMAT_GCZ:  err = gc_gcz_open(disc);      break;
        case GC_FORMAT_WIA:  err = gc_wia_open(disc, 0);   break;
        case GC_FORMAT_RVZ:  err = gc_wia_open(disc, 1);   break;
        case GC_FORMAT_WBFS: err = gc_wbfs_open(disc);     break;
        default: break;
    }

    if (err != 0) {
        fclose(disc->file);
        free(disc);
        return NULL;
    }

    return disc;
}

void gc_disc_close(GCDisc* disc) {
    if (!disc) return;
    if (disc->close) disc->close(disc);
    gc_disc_free_parsed(disc);
    if (disc->file) fclose(disc->file);
    free(disc);
}

GCDiscFormat gc_disc_format(const GCDisc* disc) {
    return disc ? disc->format : GC_FORMAT_UNKNOWN;
}

const char* gc_disc_game_id(const GCDisc* disc) {
    return disc ? disc->gameId : NULL;
}

int gc_disc_entry_count(const GCDisc* disc) {
    return disc ? (int)disc->entryCount : 0;
}

const GCEntry* gc_disc_entry(const GCDisc* disc, int index) {
    if (!disc || index < 0 || (uint32_t)index >= disc->entryCount) return NULL;
    return &disc->entries[index];
}

int gc_disc_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    if (!disc || !disc->read) return -1;
    return disc->read(disc, offset, buf, size);
}
