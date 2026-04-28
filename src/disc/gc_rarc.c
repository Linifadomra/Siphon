#include "gc_disc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "macros.h"

struct GCArc {
    unsigned char* data;
    size_t         size;
    int            owns_data;
    GCEntry*       entries;
    int            entry_count;
    char*          name_pool;
};

static GCArc* gc_arc_open_common(unsigned char* data, size_t size, int owns) {
    if (size < 0x40 || memcmp(data, "RARC", 4) != 0) return NULL;

    unsigned int header_size = gc_be32(data + 0x08);
    unsigned int data_off    = gc_be32(data + 0x0C) + header_size;
    if (header_size < 0x20 || data_off > size) return NULL;

    const unsigned char* fst = data + header_size;
    unsigned int num_dirs    = gc_be32(fst + 0x00);
    unsigned int dirs_off    = gc_be32(fst + 0x04) + header_size;
    unsigned int num_entries = gc_be32(fst + 0x08);
    unsigned int files_off   = gc_be32(fst + 0x0C) + header_size;
    unsigned int str_len     = gc_be32(fst + 0x10);
    unsigned int str_off     = gc_be32(fst + 0x14) + header_size;

    if (dirs_off + num_dirs * 0x10 > size) return NULL;
    if (files_off + num_entries * 0x14 > size) return NULL;
    if (str_off + str_len > size) return NULL;

    GCArc* arc = (GCArc*)calloc(1, sizeof(GCArc));
    if (!arc) return NULL;
    arc->data = data;
    arc->size = size;
    arc->owns_data = owns;
    arc->entry_count = (int)num_entries;
    arc->entries = (GCEntry*)calloc(num_entries ? num_entries : 1, sizeof(GCEntry));
    if (!arc->entries) { gc_arc_close(arc); return NULL; }

    const unsigned char* files = data + files_off;
    const unsigned char* dirs  = data + dirs_off;
    const char*          strs  = (const char*)(data + str_off);

    size_t pool_cap = str_len * 8; if (pool_cap < 0x10000) pool_cap = 0x10000;
    arc->name_pool = (char*)malloc(pool_cap);
    if (!arc->name_pool) { gc_arc_close(arc); return NULL; }
    size_t pool_used = 0;

    char** dir_prefix = (char**)calloc(num_dirs, sizeof(char*));
    if (!dir_prefix) { gc_arc_close(arc); return NULL; }
    dir_prefix[0] = arc->name_pool;
    arc->name_pool[pool_used++] = '\0';

    for (unsigned int d = 0; d < num_dirs; d++) {
        const unsigned char* de = dirs + d * 0x10;
        unsigned short num_child = gc_be16(de + 0x0A);
        unsigned int   first     = gc_be32(de + 0x0C);
        const char*    parent_prefix = dir_prefix[d] ? dir_prefix[d] : "";

        for (unsigned int k = 0; k < num_child; k++) {
            unsigned int idx = first + k;
            if (idx >= num_entries) continue;
            const unsigned char* fe = files + idx * 0x14;
            unsigned short type     = gc_be16(fe + 0x04);
            unsigned short name_off = gc_be16(fe + 0x06);
            unsigned int   off_field= gc_be32(fe + 0x08);
            unsigned int   size     = gc_be32(fe + 0x0C);
            const char*    name     = strs + name_off;

            if (type == 0x0200 && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) continue;

            size_t plen = strlen(parent_prefix);
            size_t nlen = strlen(name);
            size_t need = plen + (plen ? 1 : 0) + nlen + 1;
            if (pool_used + need > pool_cap) continue;
            char* full = arc->name_pool + pool_used;
            if (plen) { memcpy(full, parent_prefix, plen); full[plen] = '/'; memcpy(full + plen + 1, name, nlen); full[plen + 1 + nlen] = '\0'; }
            else      { memcpy(full, name, nlen); full[nlen] = '\0'; }
            pool_used += need;

            GCEntry* out = &arc->entries[idx];
            out->name = full;
            if (type == 0x0200) {
                out->type = GC_ENTRY_DIR;
                out->discOffset = 0;
                out->size = 0;
                // off_field on dir entries is the subdir index into the dir table.
                if (off_field < num_dirs) dir_prefix[off_field] = full;
            } else {
                out->type = GC_ENTRY_FILE;
                out->discOffset = data_off + off_field;
                out->size = size;
            }
        }
    }

    free(dir_prefix);
    return arc;
}

GCArc* gc_arc_open_mem(const void* data, size_t size) {
    return gc_arc_open_common((unsigned char*)data, size, 0);
}

GCArc* gc_arc_open_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    GCArc* arc = gc_arc_open_common(buf, (size_t)sz, 1);
    if (!arc) free(buf);
    return arc;
}

void gc_arc_close(GCArc* arc) {
    if (!arc) return;
    if (arc->owns_data) free(arc->data);
    free(arc->entries);
    free(arc->name_pool);
    free(arc);
}

int gc_arc_entry_count(const GCArc* arc) {
    return arc ? arc->entry_count : 0;
}

const GCEntry* gc_arc_entry(const GCArc* arc, int index) {
    if (!arc || index < 0 || index >= arc->entry_count) return NULL;
    return &arc->entries[index];
}

static void mkdir_p(const char* path) {
    char buf[4096];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            MKDIR_ONE(buf);
            buf[i] = '/';
        }
    }
    MKDIR_ONE(buf);
}

int gc_arc_extract_all(GCArc* arc, const char* outputDir) {
    if (!arc || !outputDir) return -1;
    mkdir_p(outputDir);

    char path[4096];
    size_t olen = strlen(outputDir);
    for (int i = 0; i < arc->entry_count; i++) {
        const GCEntry* e = &arc->entries[i];
        if (!e->name) continue;

        size_t nlen = strlen(e->name);
        if (olen + 1 + nlen + 1 > sizeof(path)) return -1;
        memcpy(path, outputDir, olen);
        path[olen] = '/';
        memcpy(path + olen + 1, e->name, nlen + 1);

        if (e->type == GC_ENTRY_DIR) {
            mkdir_p(path);
            continue;
        }

        for (size_t j = olen + 1 + nlen; j > olen; j--) {
            if (path[j] == '/') {
                path[j] = '\0';
                mkdir_p(path);
                path[j] = '/';
                break;
            }
        }
        FILE* f = fopen(path, "wb");
        if (!f) return -1;
        if (fwrite(arc->data + e->discOffset, 1, e->size, f) != e->size) {
            fclose(f);
            return -1;
        }
        fclose(f);
    }
    return 0;
}

int gc_arc_read_file(GCArc* arc, int index, void** out_data, size_t* out_size) {
    if (!arc || index < 0 || index >= arc->entry_count) return -1;
    const GCEntry* e = &arc->entries[index];
    if (e->type != GC_ENTRY_FILE) return -1;
    if ((size_t)e->discOffset + e->size > arc->size) return -1;
    void* buf = malloc(e->size);
    if (!buf) return -1;
    memcpy(buf, arc->data + e->discOffset, e->size);
    *out_data = buf;
    *out_size = e->size;
    return 0;
}
