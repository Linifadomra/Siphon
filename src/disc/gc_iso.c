#include "gc_disc_internal.h"
#include <string.h>

static int iso_read(GCDisc* disc, uint32_t offset, void* buf, size_t size) {
    if (fseek(disc->file, offset, SEEK_SET) != 0) return -1;
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
