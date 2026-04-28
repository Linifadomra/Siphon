#ifndef SIPHON_H
#define SIPHON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIPHON_OK              = 0,
    SIPHON_ERR_IO          = 1,
    SIPHON_ERR_FORMAT      = 2,
    SIPHON_ERR_NOT_FOUND   = 3,
    SIPHON_ERR_ID_MISMATCH = 4,
    SIPHON_ERR_CORRUPT     = 5,
} SiphonError;

typedef void (*SiphonLogFn)(void* userdata, const char* msg);

typedef struct {
    char game_id[8];        // 6-char ID, null-terminated
    char format[16];        // "ISO/GCM", "CISO", "GCZ", "WIA", "RVZ", "WBFS"
    int  entry_count;
} SiphonDiscInfo;

SiphonError siphon_disc_inspect(const char* image, SiphonDiscInfo* out, SiphonLogFn log, void* userdata);

SiphonError siphon_disc_extract(
    const char* image,
    const char* outdir,
    const char* const* expect_ids,
    size_t num_ids,
    SiphonLogFn log,
    void* userdata
);

SiphonError siphon_arc_extract(const char* archive, const char* outdir, SiphonLogFn log, void* userdata);
SiphonError siphon_arc_list(const char* archive, SiphonLogFn log, void* userdata);
SiphonError siphon_arc_copy(const char* archive, const char* inner, const char* out_path, SiphonLogFn log, void* userdata);
SiphonError siphon_dol_split(const char* config, const char* outdir, SiphonLogFn log, void* userdata);
SiphonError siphon_yaz0_decompress_file(const char* in, const char* out, SiphonLogFn log, void* userdata);

#ifdef __cplusplus
}
#endif

#endif
