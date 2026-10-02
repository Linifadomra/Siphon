#include "siphon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void silent_log(void* u, const char* m) { (void)u; (void)m; }

int main(void) {
    const char* disc_path = SIPHON_TEST_ROOT "/test/fixtures/mock/gc/minimal.iso";
    const char* file_path = "test.txt";

    void* data = NULL;
    size_t size = 0;

    SiphonError err = siphon_disc_read_file(disc_path, file_path, &data, &size, silent_log, NULL);
    if (err != SIPHON_OK) {
        fprintf(stderr, "siphon_disc_read_file failed with error %d\n", err);
        return 1;
    }

    const char* expected = "Hello from GameCube!\n";
    size_t expected_size = strlen(expected);

    if (size != expected_size) {
        fprintf(stderr, "size mismatch: got %zu, expected %zu\n", size, expected_size);
        free(data);
        return 1;
    }

    if (memcmp(data, expected, size) != 0) {
        fprintf(stderr, "content mismatch\n");
        fprintf(stderr, "expected: %.*s\n", (int)expected_size, expected);
        fprintf(stderr, "got:      %.*s\n", (int)size, (char*)data);
        free(data);
        return 1;
    }

    free(data);

    err = siphon_disc_read_file(disc_path, "nonexistent.bin", &data, &size, silent_log, NULL);
    if (err != SIPHON_ERR_NOT_FOUND) {
        fprintf(stderr, "expected SIPHON_ERR_NOT_FOUND for missing file, got %d\n", err);
        return 1;
    }

    printf("all tests passed\n");
    return 0;
}
