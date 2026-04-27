#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc_disc.h"
#include "gc_dol.h"
#include "gc_yaz0.h"

static const char* format_name(GCDiscFormat fmt) {
    switch (fmt) {
        case GC_FORMAT_ISO:  return "ISO/GCM";
        case GC_FORMAT_CISO: return "CISO";
        case GC_FORMAT_GCZ:  return "GCZ";
        case GC_FORMAT_WIA:  return "WIA";
        case GC_FORMAT_RVZ:  return "RVZ";
        case GC_FORMAT_WBFS: return "WBFS";
        default:             return "unknown";
    }
}

static int usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  siphon disc <image> <outdir> [--expect-id ID]\n"
        "  siphon arc  <archive> <outdir>             extract all\n"
        "  siphon arc  ls <archive>                   list entries\n"
        "  siphon arc  cp <archive>:<inner> <out>     extract one entry\n"
        "  siphon dol  split <config.yml> <outdir>\n"
        "  siphon yaz0 decompress <in> -o <out>\n"
        "  siphon yaz0 decompress <in> <out>\n");
    return 1;
}

static int cmd_disc(int argc, char* argv[]) {
    const char* image = NULL;
    const char* outdir = NULL;
    const char* expect_id = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--expect-id") == 0 && i + 1 < argc) {
            expect_id = argv[++i];
        } else if (!image) {
            image = argv[i];
        } else if (!outdir) {
            outdir = argv[i];
        } else {
            return usage();
        }
    }
    if (!image || !outdir) return usage();

    GCDiscFormat fmt = gc_disc_detect_format(image);
    printf("Format: %s\n", format_name(fmt));
    if (fmt == GC_FORMAT_UNKNOWN) {
        fprintf(stderr, "Error: unrecognized disc image format\n");
        return 1;
    }
    GCDisc* disc = gc_disc_open(image);
    if (!disc) {
        fprintf(stderr, "Error: failed to open disc image\n");
        return 1;
    }

    const char* id = gc_disc_game_id(disc);
    printf("Game ID: %s\n", id);
    if (expect_id && strncmp(id, expect_id, 6) != 0) {
        fprintf(stderr, "Error: game ID %s does not match expected %s\n", id, expect_id);
        gc_disc_close(disc);
        return 1;
    }
    printf("Entries: %d\n", gc_disc_entry_count(disc));

    int rc = gc_disc_extract_all(disc, outdir);
    gc_disc_close(disc);
    if (rc != 0) {
        fprintf(stderr, "Error: extraction failed\n");
        return 1;
    }
    printf("Extraction complete.\n");
    return 0;
}

static int slurp_file(const char* path, unsigned char** out, size_t* out_n) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);
    *out = buf;
    *out_n = (size_t)sz;
    return 0;
}

static int write_file(const char* path, const void* data, size_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int cmd_arc_extract(const char* in, const char* outdir) {
    GCArc* arc = gc_arc_open_file(in);
    if (!arc) {
        fprintf(stderr, "Error: failed to open archive %s\n", in);
        return 1;
    }
    int rc = gc_arc_extract_all(arc, outdir);
    gc_arc_close(arc);
    if (rc != 0) {
        fprintf(stderr, "Error: archive extraction failed\n");
        return 1;
    }
    printf("Archive extraction complete.\n");
    return 0;
}

static int cmd_arc_ls(const char* archive) {
    GCArc* arc = gc_arc_open_file(archive);
    if (!arc) {
        fprintf(stderr, "Error: failed to open archive %s\n", archive);
        return 1;
    }
    int n = gc_arc_entry_count(arc);
    for (int i = 0; i < n; i++) {
        const GCEntry* e = gc_arc_entry(arc, i);
        if (!e || !e->name) continue;
        if (e->type == GC_ENTRY_FILE) printf("%s\n", e->name);
    }
    gc_arc_close(arc);
    return 0;
}

static int cmd_arc_cp(const char* spec, const char* out_path) {
    const char* colon = strchr(spec, ':');
    if (!colon) {
        fprintf(stderr, "Error: arc cp requires <archive>:<inner_path>\n");
        return 1;
    }
    char arc_path[1024];
    size_t arc_len = (size_t)(colon - spec);
    if (arc_len >= sizeof(arc_path)) return 1;
    memcpy(arc_path, spec, arc_len);
    arc_path[arc_len] = '\0';
    const char* inner = colon + 1;

    GCArc* arc = gc_arc_open_file(arc_path);
    if (!arc) {
        fprintf(stderr, "Error: failed to open archive %s\n", arc_path);
        return 1;
    }
    int found = -1;
    int n = gc_arc_entry_count(arc);
    for (int i = 0; i < n; i++) {
        const GCEntry* e = gc_arc_entry(arc, i);
        if (!e || !e->name || e->type != GC_ENTRY_FILE) continue;
        if (strcmp(e->name, inner) == 0) { found = i; break; }
    }
    if (found < 0) {
        fprintf(stderr, "Error: %s not found inside %s\n", inner, arc_path);
        gc_arc_close(arc);
        return 1;
    }
    void* buf = NULL;
    size_t sz = 0;
    if (gc_arc_read_file(arc, found, &buf, &sz) != 0) {
        gc_arc_close(arc);
        return 1;
    }
    gc_arc_close(arc);
    int rc = write_file(out_path, buf, sz);
    free(buf);
    if (rc != 0) {
        fprintf(stderr, "Error: write %s failed\n", out_path);
        return 1;
    }
    return 0;
}

static int cmd_arc(int argc, char* argv[]) {
    if (argc >= 2 && strcmp(argv[0], "ls") == 0) return cmd_arc_ls(argv[1]);
    if (argc == 3 && strcmp(argv[0], "cp") == 0) return cmd_arc_cp(argv[1], argv[2]);
    if (argc == 2) return cmd_arc_extract(argv[0], argv[1]);
    return usage();
}

static int cmd_dol(int argc, char* argv[]) {
    if (argc != 3 || strcmp(argv[0], "split") != 0) return usage();
    return gc_dol_split(argv[1], argv[2]);
}

static int cmd_yaz0(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[0], "decompress") != 0) return usage();
    const char* in = argv[1];
    const char* out = NULL;
    if (argc == 4 && strcmp(argv[2], "-o") == 0) out = argv[3];
    else if (argc == 3) out = argv[2];
    else return usage();

    unsigned char* data = NULL;
    size_t n = 0;
    if (slurp_file(in, &data, &n) != 0) {
        fprintf(stderr, "Error: cannot read %s\n", in);
        return 1;
    }
    unsigned char* dec = NULL;
    size_t dec_n = 0;
    if (gc_yaz0_is_compressed(data, n)) {
        if (gc_yaz0_decompress(data, n, &dec, &dec_n) != 0) {
            fprintf(stderr, "Error: yaz0 decompress failed\n");
            free(data);
            return 1;
        }
    } else {
        // Pass through if not actually compressed.
        dec = data;
        dec_n = n;
        data = NULL;
    }
    int rc = write_file(out, dec, dec_n);
    free(dec);
    free(data);
    if (rc != 0) {
        fprintf(stderr, "Error: write %s failed\n", out);
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return usage();
    const char* sub = argv[1];
    if      (strcmp(sub, "disc") == 0) return cmd_disc(argc - 2, argv + 2);
    else if (strcmp(sub, "arc")  == 0) return cmd_arc (argc - 2, argv + 2);
    else if (strcmp(sub, "dol")  == 0) return cmd_dol (argc - 2, argv + 2);
    else if (strcmp(sub, "yaz0") == 0) return cmd_yaz0(argc - 2, argv + 2);
    return usage();
}
