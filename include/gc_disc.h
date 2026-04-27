#ifndef GC_DISC_H
#define GC_DISC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GCDisc GCDisc;

typedef enum {
    GC_FORMAT_UNKNOWN = 0,
    GC_FORMAT_ISO,
    GC_FORMAT_CISO,
    GC_FORMAT_GCZ,
    GC_FORMAT_WIA,
    GC_FORMAT_RVZ,
    GC_FORMAT_WBFS,
} GCDiscFormat;

typedef enum {
    GC_ENTRY_FILE = 0,
    GC_ENTRY_DIR  = 1,
} GCEntryType;

typedef struct {
    GCEntryType type;
    const char* name;       // full path relative to files/
    uint32_t    discOffset; // files only
    uint32_t    size;       // file size, or next-entry index for dirs
} GCEntry;

GCDiscFormat gc_disc_detect_format(const char* path);
GCDisc* gc_disc_open(const char* path);
void gc_disc_close(GCDisc* disc);
GCDiscFormat gc_disc_format(const GCDisc* disc);
const char* gc_disc_game_id(const GCDisc* disc);
int gc_disc_entry_count(const GCDisc* disc);
const GCEntry* gc_disc_entry(const GCDisc* disc, int index);
int gc_disc_read(GCDisc* disc, uint32_t offset, void* buf, size_t size);
int gc_disc_extract_all(GCDisc* disc, const char* outputDir);
int gc_disc_extract_file(GCDisc* disc, int index, const char* outputPath);

typedef struct GCArc GCArc;

GCArc* gc_arc_open_file(const char* path);
// data is borrowed; must outlive the GCArc.
GCArc* gc_arc_open_mem(const void* data, size_t size);
void gc_arc_close(GCArc* arc);
int gc_arc_entry_count(const GCArc* arc);
const GCEntry* gc_arc_entry(const GCArc* arc, int index);
int gc_arc_extract_all(GCArc* arc, const char* outputDir);
// Allocates a copy of entry `index`'s file content via malloc. Caller frees.
int gc_arc_read_file(GCArc* arc, int index, void** out_data, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif
