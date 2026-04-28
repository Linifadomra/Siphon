#include "siphon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc_disc.h"
#include "gc_dol.h"
#include "gc_yaz0.h"
#include "siphon_log.h"

static void install_logger(SiphonLogFn fn, void* ud) {
    siphon_log_set((siphon_log_fn)fn, ud);
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

SiphonError siphon_disc_inspect(const char* image, SiphonDiscInfo* out, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    if (!out) return SIPHON_ERR_IO;
    memset(out, 0, sizeof(*out));

    GCDiscFormat fmt = gc_disc_detect_format(image);
    if (fmt == GC_FORMAT_UNKNOWN) {
        siphon_log("Error: unrecognized disc image format");
        return SIPHON_ERR_FORMAT;
    }
    GCDisc* disc = gc_disc_open(image);
    if (!disc) {
        siphon_log("Error: failed to open disc image");
        return SIPHON_ERR_IO;
    }
    snprintf(out->format, sizeof(out->format), "%s", format_name(fmt));
    const char* id = gc_disc_game_id(disc);
    if (id) snprintf(out->game_id, sizeof(out->game_id), "%s", id);
    out->entry_count = gc_disc_entry_count(disc);
    gc_disc_close(disc);
    return SIPHON_OK;
}

SiphonError siphon_disc_extract(
    const char* image,
    const char* outdir,
    const char* expect_id,
    SiphonLogFn log,
    void* userdata
) {
    install_logger(log, userdata);
    GCDiscFormat fmt = gc_disc_detect_format(image);
    siphon_log("Format: %s", format_name(fmt));
    if (fmt == GC_FORMAT_UNKNOWN) {
        siphon_log("Error: unrecognized disc image format");
        return SIPHON_ERR_FORMAT;
    }

    GCDisc* disc = gc_disc_open(image);
    if (!disc) {
        siphon_log("Error: failed to open disc image");
        return SIPHON_ERR_IO;
    }

    const char* id = gc_disc_game_id(disc);
    siphon_log("Game ID: %s", id);
    if (expect_id && strncmp(id, expect_id, 6) != 0) {
        siphon_log("Error: game ID %s does not match expected %s", id, expect_id);
        gc_disc_close(disc);
        return SIPHON_ERR_ID_MISMATCH;
    }
    siphon_log("Entries: %d", gc_disc_entry_count(disc));

    int rc = gc_disc_extract_all(disc, outdir);
    gc_disc_close(disc);
    if (rc != 0) {
        siphon_log("Error: extraction failed");
        // Note: this will want to return a structured SiphonError down the line.
        return SIPHON_ERR_IO;
    }
    siphon_log("Extraction complete.");
    return SIPHON_OK;
}

SiphonError siphon_arc_extract(const char* archive, const char* outdir, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    GCArc* arc = gc_arc_open_file(archive);
    if (!arc) {
        siphon_log("Error: failed to open archive %s", archive);
        return SIPHON_ERR_IO;
    }
    int rc = gc_arc_extract_all(arc, outdir);
    gc_arc_close(arc);
    if (rc != 0) {
        siphon_log("Error: archive extraction failed");
        // Note: this will want to return a structured SiphonError down the line.
        return SIPHON_ERR_IO;
    }
    siphon_log("Archive extraction complete.");
    return SIPHON_OK;
}

SiphonError siphon_arc_list(const char* archive, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    GCArc* arc = gc_arc_open_file(archive);
    if (!arc) {
        siphon_log("Error: failed to open archive %s", archive);
        return SIPHON_ERR_IO;
    }
    int n = gc_arc_entry_count(arc);
    for (int i = 0; i < n; i++) {
        const GCEntry* e = gc_arc_entry(arc, i);
        if (!e || !e->name) continue;
        if (e->type == GC_ENTRY_FILE) siphon_log("%s", e->name);
    }
    gc_arc_close(arc);
    return SIPHON_OK;
}

SiphonError siphon_arc_copy(const char* archive, const char* inner, const char* out_path, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    GCArc* arc = gc_arc_open_file(archive);
    if (!arc) {
        siphon_log("Error: failed to open archive %s", archive);
        return SIPHON_ERR_IO;
    }
    int found = -1;
    int n = gc_arc_entry_count(arc);
    for (int i = 0; i < n; i++) {
        const GCEntry* e = gc_arc_entry(arc, i);
        if (!e || !e->name || e->type != GC_ENTRY_FILE) continue;
        if (strcmp(e->name, inner) == 0) { found = i; break; }
    }
    if (found < 0) {
        siphon_log("Error: %s not found inside %s", inner, archive);
        gc_arc_close(arc);
        return SIPHON_ERR_NOT_FOUND;
    }
    void* buf = NULL;
    size_t sz = 0;
    if (gc_arc_read_file(arc, found, &buf, &sz) != 0) {
        gc_arc_close(arc);
        // Note: this will want to return a structured SiphonError down the line.
        return SIPHON_ERR_IO;
    }
    gc_arc_close(arc);
    int rc = write_file(out_path, buf, sz);
    free(buf);
    if (rc != 0) {
        siphon_log("Error: write %s failed", out_path);
        return SIPHON_ERR_IO;
    }
    return SIPHON_OK;
}

SiphonError siphon_dol_split(const char* config, const char* outdir, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    int rc = gc_dol_split(config, outdir);
    if (rc != 0) {
        siphon_log("Error: dol split failed");
        return SIPHON_ERR_IO;
    }
    siphon_log("DOL split complete");
    return SIPHON_OK;
}

SiphonError siphon_yaz0_decompress_file(const char* in, const char* out, SiphonLogFn log, void* userdata) {
    install_logger(log, userdata);
    unsigned char* data = NULL;
    size_t n = 0;
    if (slurp_file(in, &data, &n) != 0) {
        siphon_log("Error: cannot read %s", in);
        return SIPHON_ERR_IO;
    }
    unsigned char* dec = NULL;
    size_t dec_n = 0;
    if (gc_yaz0_is_compressed(data, n)) {
        if (gc_yaz0_decompress(data, n, &dec, &dec_n) != 0) {
            siphon_log("Error: yaz0 decompress failed");
            free(data);
            // Note: this will want to return a structured SiphonError down the line.
            return SIPHON_ERR_IO;
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
        siphon_log("Error: write %s failed", out);
        return SIPHON_ERR_IO;
    }
    return SIPHON_OK;
}