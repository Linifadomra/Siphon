#ifndef GC_DISC_INTERNAL_H
#define GC_DISC_INTERNAL_H

#include <stdio.h>
#include <confluence/types.h>

typedef struct GCDisc GCDisc;

typedef int (*gc_read_fn)(GCDisc* disc, uint64_t offset, void* buf, size_t size);
typedef void (*gc_close_fn)(GCDisc* disc);

typedef enum {
    GC_FORMAT_UNKNOWN = 0,
    GC_FORMAT_ISO,
    GC_FORMAT_CISO,
    GC_FORMAT_GCZ,
    GC_FORMAT_WIA,
    GC_FORMAT_RVZ,
    GC_FORMAT_WBFS,
} GCDiscFormat;

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

    uint32_t offsetShift;
    void*    wii;
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
int gc_wii_wrap(GCDisc* disc);
void gc_wii_free(GCDisc* disc);

FILE*        gc_disc_fopen(const char* path);
GCDiscFormat gc_disc_detect_format(const char* path);
GCDisc*      gc_disc_open(const char* path);
void         gc_disc_close(GCDisc* disc);
GCDiscFormat gc_disc_format(const GCDisc* disc);
const char*  gc_disc_game_id(const GCDisc* disc);
int          gc_disc_entry_count(const GCDisc* disc);
const GCEntry* gc_disc_entry(const GCDisc* disc, int index);
int          gc_disc_read(GCDisc* disc, uint32_t offset, void* buf, size_t size);
int          gc_disc_extract_all(GCDisc* disc, const char* outputDir);
int          gc_disc_extract_file(GCDisc* disc, int index, const char* outputPath);

#endif
