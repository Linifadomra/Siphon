#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "gc_disc_internal.h"
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
    // Windows equivalents for POSIX types and functions
    typedef long long off_t;
    #define ftello _ftelli64
    #define fseeko _fseeki64
#endif

static int iso_read(GCDisc* disc, uint64_t offset, void* buf, size_t size) {
    if (fseeko(disc->file, (off_t)offset, SEEK_SET) != 0) return -1;
    if (fread(buf, 1, size, disc->file) != size) return -1;
    return 0;
}

static void iso_close(GCDisc* disc) {
    (void)disc;
}

int gc_iso_open(GCDisc* disc) {
    disc->read = iso_read;
    disc->close = iso_close;
    return gc_disc_parse_fst(disc);
}
