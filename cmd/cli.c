#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "siphon.h"

static const char* siphon_error_str(SiphonError err) {
    switch (err) {
        case SIPHON_OK:             return "ok";
        case SIPHON_ERR_IO:         return "io error";
        case SIPHON_ERR_FORMAT:     return "unrecognized format";
        case SIPHON_ERR_NOT_FOUND:  return "not found";
        case SIPHON_ERR_ID_MISMATCH:return "game ID mismatch";
        case SIPHON_ERR_CORRUPT:    return "corrupt data";
        default:                    return "unknown error";
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

static void cli_log(void* userdata, const char* msg) {
    (void)userdata; // Unused in this context :)
    puts(msg);
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

    const char* const* ids = expect_id ? &expect_id : NULL;
    size_t num_ids = expect_id ? 1 : 0;
    SiphonError err = siphon_disc_extract(image, outdir, ids, num_ids, cli_log, NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
        return 1;
    }
    return 0;
}

static int cmd_arc_extract(const char* in, const char* outdir) {
    SiphonError err = siphon_arc_extract(in, outdir, cli_log, NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
        return 1;
    }
    return 0;
}

static int cmd_arc_ls(const char* archive) {
    SiphonError err = siphon_arc_list(archive, cli_log, NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
        return 1;
    }
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

    SiphonError err = siphon_arc_copy(arc_path, inner, out_path,cli_log,NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
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
    SiphonError err = siphon_dol_split(argv[1], argv[2], cli_log, NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
        return 1;
    }
    return 0;
}

static int cmd_yaz0(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[0], "decompress") != 0) return usage();
    const char* in = argv[1];
    const char* out = NULL;
    if (argc == 4 && strcmp(argv[2], "-o") == 0) out = argv[3];
    else if (argc == 3) out = argv[2];
    else return usage();
    SiphonError err = siphon_yaz0_decompress_file(in, out,cli_log,NULL);
    if (err) {
        fprintf(stderr, "Error: %s\n", siphon_error_str(err));
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
