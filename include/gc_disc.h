#ifndef GC_DISC_H
#define GC_DISC_H
#include <stdint.h>
#include <stddef.h>
#include <confluence/types.h>
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

#ifdef __cplusplus
}
#endif
#endif