#include "siphon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void silent(void* u, const char* m) { (void)u; (void)m; }

static int slurp_file(const char* path, unsigned char** out, size_t* out_n) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_n = (size_t)sz;
    return 0;
}

int main(void) {
    const char* disc_path = getenv("SIPHON_TEST_DISC");
    const char* file_path = getenv("SIPHON_TEST_FILE_PATH");
    const char* ref_path = getenv("SIPHON_TEST_FILE_REF");

    if (!disc_path || !*disc_path || !file_path || !*file_path || !ref_path || !*ref_path) {
        printf("skipped: set SIPHON_TEST_DISC, SIPHON_TEST_FILE_PATH, and SIPHON_TEST_FILE_REF to run\n");
        printf("example:\n");
        printf("  SIPHON_TEST_DISC=/path/to/game.iso \\\n");
        printf("  SIPHON_TEST_FILE_PATH=opening.bnr \\\n");
        printf("  SIPHON_TEST_FILE_REF=/path/to/extracted/opening.bnr \\\n");
        printf("  ./test_disc_read_file_local\n");
        return 77;
    }

    void* disc_data = NULL;
    size_t disc_size = 0;
    SiphonError err = siphon_disc_read_file(disc_path, file_path, &disc_data, &disc_size, silent, NULL);
    if (err != SIPHON_OK) {
        fprintf(stderr, "siphon_disc_read_file failed with error %d\n", err);
        return 1;
    }

    unsigned char* ref_data = NULL;
    size_t ref_size = 0;
    if (slurp_file(ref_path, &ref_data, &ref_size) != 0) {
        fprintf(stderr, "failed to read reference file: %s\n", ref_path);
        free(disc_data);
        return 1;
    }

    if (disc_size != ref_size) {
        fprintf(stderr, "size mismatch: disc=%zu ref=%zu\n", disc_size, ref_size);
        free(disc_data);
        free(ref_data);
        return 1;
    }

    if (memcmp(disc_data, ref_data, disc_size) != 0) {
        fprintf(stderr, "content mismatch between disc file and reference file\n");
        for (size_t i = 0; i < disc_size && i < 256; i++) {
            if (((unsigned char*)disc_data)[i] != ref_data[i]) {
                fprintf(stderr, "first difference at byte %zu: disc=0x%02X ref=0x%02X\n",
                        i, ((unsigned char*)disc_data)[i], ref_data[i]);
                break;
            }
        }
        free(disc_data);
        free(ref_data);
        return 1;
    }

    free(disc_data);
    free(ref_data);

    printf("ok: '%s' matches (size=%zu)\n", file_path, disc_size);
    return 0;
}
