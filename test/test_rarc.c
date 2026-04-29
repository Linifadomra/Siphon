#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
  #include <direct.h>
  #define MKDIR(p) _mkdir(p)
#else
  #include <unistd.h>
  #define MKDIR(p) mkdir((p), 0755)
#endif
#include "gc_disc.h"

/*
 * Build a minimal valid RARC blob in memory.
 *
 * Layout (all big-endian):
 *   0x0000  Header      (0x20)
 *   0x0020  FST info    (0x20)
 *   0x0040  Dir table   (1 dir  × 0x10 = 0x10)
 *   0x0050  File table  (4 entries × 0x14 = 0x50)
 *   0x00A0  String table ("ROOT\0.\0..\0file.bin\0" = 0x14)
 *   0x00B4  File data   (0x04)
 *   total   0x00B8
 *
 * 4 file-table entries:
 *   0 – "."        (self-ref dir)
 *   1 – ".."       (parent-ref dir)
 *   2 – root dir   (dir entry, points back to dir index 0)
 *   3 – file.bin   (file)
 */
static void wr32(unsigned char* p, unsigned int v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
}
static void wr16(unsigned char* p, unsigned short v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}

static int build_minimal_rarc(unsigned char** out, size_t* out_size) {
    /* offsets */
    const unsigned int HDR      = 0x0000;
    const unsigned int FST_INFO = 0x0020;
    const unsigned int DIR_TBL  = 0x0040;
    const unsigned int FILE_TBL = 0x0050;
    const unsigned int STR_TBL  = 0x00A0;
    const unsigned int DATA     = 0x00B4;
    const unsigned int TOTAL    = 0x00B8;

    /* string table layout: ROOT\0.\0..\0file.bin\0 */
    const unsigned int STR_ROOT     = 0x00; /* "ROOT"     */
    const unsigned int STR_DOT      = 0x05; /* "."        */
    const unsigned int STR_DOTDOT   = 0x07; /* ".."       */
    const unsigned int STR_FILE     = 0x0A; /* "file.bin" */
    const unsigned int STR_LEN      = 0x14;

    const unsigned int NUM_DIRS     = 1;
    const unsigned int NUM_ENTRIES  = 4;

    unsigned char* buf = (unsigned char*)calloc(1, TOTAL);
    if (!buf) return -1;

    /* ---- Header ---- */
    memcpy(buf + HDR + 0x00, "RARC", 4);
    wr32(buf + HDR + 0x04, TOTAL);           /* file size */
    wr32(buf + HDR + 0x08, 0x20);            /* header size */
    wr32(buf + HDR + 0x0C, DATA - 0x20);     /* data offset relative to end-of-header */
    wr32(buf + HDR + 0x10, 0x04);            /* data length */
    wr32(buf + HDR + 0x14, 0x04);            /* data length copy */

    /* ---- FST info (at 0x20) ---- */
    /* All offsets here are relative to FST info start (0x20). */
    unsigned char* fst = buf + FST_INFO;
    wr32(fst + 0x00, NUM_DIRS);
    wr32(fst + 0x04, DIR_TBL  - FST_INFO);   /* dirs_offset  */
    wr32(fst + 0x08, NUM_ENTRIES);
    wr32(fst + 0x0C, FILE_TBL - FST_INFO);   /* files_offset */
    wr32(fst + 0x10, STR_LEN);
    wr32(fst + 0x14, STR_TBL  - FST_INFO);   /* string_table_offset */
    wr16(fst + 0x18, 1);                      /* num_files_written (just the real file) */

    /* ---- Dir table (1 entry at 0x40) ---- */
    unsigned char* dir = buf + DIR_TBL;
    /* id = 0xFFFFFFFF (root), name_hash, ?, num_entries, first_entry_index */
    wr32(dir + 0x00, 0xFFFFFFFF);
    wr32(dir + 0x04, 0x52415243); /* hash placeholder */
    wr16(dir + 0x08, STR_ROOT);
    wr16(dir + 0x0A, NUM_ENTRIES);
    wr32(dir + 0x0C, 0);           /* first_entry_index = 0 */

    /* ---- File table (4 entries at 0x50, each 0x14 bytes) ---- */
    unsigned char* fe = buf + FILE_TBL;

    /* entry 0: "." self-ref dir */
    wr16(fe + 0x00, 0xFFFF);           /* id = dir */
    wr16(fe + 0x02, 0x0000);
    wr16(fe + 0x04, 0x0200);           /* type = dir */
    wr16(fe + 0x06, STR_DOT);
    wr32(fe + 0x08, 0);                /* data_offset = dir index 0 */
    wr32(fe + 0x0C, 0);
    fe += 0x14;

    /* entry 1: ".." parent-ref dir */
    wr16(fe + 0x00, 0xFFFF);
    wr16(fe + 0x02, 0x0000);
    wr16(fe + 0x04, 0x0200);
    wr16(fe + 0x06, STR_DOTDOT);
    wr32(fe + 0x08, 0xFFFFFFFF);      /* no parent = -1 */
    wr32(fe + 0x0C, 0);
    fe += 0x14;

    /* entry 2: root dir entry (dir) */
    wr16(fe + 0x00, 0xFFFF);
    wr16(fe + 0x02, 0x0000);
    wr16(fe + 0x04, 0x0200);
    wr16(fe + 0x06, STR_ROOT);
    wr32(fe + 0x08, 0);
    wr32(fe + 0x0C, 0);
    fe += 0x14;

    /* entry 3: "file.bin" actual file */
    wr16(fe + 0x00, 0x0000);           /* id = 0 */
    wr16(fe + 0x02, 0x0000);
    wr16(fe + 0x04, 0x1100);           /* type = file */
    wr16(fe + 0x06, STR_FILE);
    wr32(fe + 0x08, 0);                /* data_offset from data start */
    wr32(fe + 0x0C, 4);               /* size */

    /* ---- String table (at 0xA0) ---- */
    unsigned char* st = buf + STR_TBL;
    memcpy(st + STR_ROOT,   "ROOT",     4); st[STR_ROOT   + 4] = '\0';
    memcpy(st + STR_DOT,    ".",        1); st[STR_DOT    + 1] = '\0';
    memcpy(st + STR_DOTDOT, "..",       2); st[STR_DOTDOT + 2] = '\0';
    memcpy(st + STR_FILE,   "file.bin", 8); st[STR_FILE   + 8] = '\0';

    /* ---- File data (at 0xB4) ---- */
    buf[DATA + 0] = 0xDE;
    buf[DATA + 1] = 0xAD;
    buf[DATA + 2] = 0xBE;
    buf[DATA + 3] = 0xEF;

    *out = buf;
    *out_size = TOTAL;
    return 0;
}

static int test_walk_entries(void) {
    unsigned char* buf = NULL; size_t sz = 0;
    if (build_minimal_rarc(&buf, &sz) != 0) return 1;
    GCArc* arc = gc_arc_open_mem(buf, sz);
    if (!arc) { free(buf); return 1; }

    int n = gc_arc_entry_count(arc);
    int found_file = 0;
    for (int i = 0; i < n; i++) {
        const GCEntry* e = gc_arc_entry(arc, i);
        if (!e || !e->name) continue;
        if (e->type == GC_ENTRY_FILE && e->size > 0) found_file = 1;
    }
    gc_arc_close(arc); free(buf);
    if (!found_file) { fprintf(stderr, "no file entries found\n"); return 1; }
    return 0;
}

static int test_open_from_mem(void) {
    unsigned char* buf = NULL;
    size_t sz = 0;
    if (build_minimal_rarc(&buf, &sz) != 0) {
        fprintf(stderr, "build_minimal_rarc failed\n");
        return 1;
    }
    GCArc* arc = gc_arc_open_mem(buf, sz);
    if (!arc) { free(buf); fprintf(stderr, "open failed\n"); return 1; }
    int n = gc_arc_entry_count(arc);
    if (n < 2) {
        fprintf(stderr, "entry_count=%d, expected >=2\n", n);
        gc_arc_close(arc); free(buf); return 1;
    }
    gc_arc_close(arc);
    free(buf);
    return 0;
}

static int load_file(const char* path, unsigned char** out, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);
    *out = buf; *out_size = (size_t)sz;
    return 0;
}

static int test_extract_all(void) {
    unsigned char* buf = NULL; size_t sz = 0;
    if (load_file("test/fixtures/minimal.arc", &buf, &sz) != 0) {
        fprintf(stderr, "fixture minimal.arc not found\n");
        return 1;
    }
    GCArc* arc = gc_arc_open_mem(buf, sz);
    if (!arc) { free(buf); fprintf(stderr, "open failed\n"); return 1; }

    const char* outdir = "test/out";
    MKDIR("test"); MKDIR(outdir);

    int rc = gc_arc_extract_all(arc, outdir);
    gc_arc_close(arc); free(buf);
    if (rc != 0) { fprintf(stderr, "extract_all returned %d\n", rc); return 1; }

    unsigned char* got = NULL; size_t got_sz = 0;
    if (load_file("test/out/tex/sw_rope.bti", &got, &got_sz) != 0) {
        fprintf(stderr, "tex/sw_rope.bti not extracted\n");
        return 1;
    }
    if (got_sz != 160) {
        fprintf(stderr, "extracted size %zu, expected 160\n", got_sz);
        free(got); return 1;
    }
    free(got);
    return 0;
}

int main(void) {
    int failures = 0;
    if (test_open_from_mem() != 0) { failures++; fprintf(stderr, "FAIL test_open_from_mem\n"); }
    if (test_walk_entries() != 0) { failures++; fprintf(stderr, "FAIL test_walk_entries\n"); }
    if (test_extract_all() != 0) { failures++; fprintf(stderr, "FAIL test_extract_all\n"); }
    if (failures == 0) printf("all tests passed\n");
    return failures;
}
