#include "gc_yaml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fail++; } \
} while (0)

static int seq_count(const GCYamlNode* n) {
    int c = 0;
    if (n) for (const GCYamlNode* x = n->children; x; x = x->next) c++;
    return c;
}

static void test_basic(void) {
    GCYamlNode* r = gc_yaml_parse_file("test/fixtures/basic.yml");
    CHECK(r != NULL, "parse failed");
    CHECK(strcmp(gc_yaml_get_str(r, "name"), "framework") == 0, "name");
    CHECK(strcmp(gc_yaml_get_str(r, "version"), "1") == 0, "version");

    const GCYamlNode* ex = gc_yaml_find(r, "extract");
    CHECK(ex && ex->kind == GC_YAML_SEQUENCE, "extract is sequence");
    CHECK(seq_count(ex) == 2, "extract count = %d", seq_count(ex));

    const GCYamlNode* first = ex->children;
    CHECK(strcmp(gc_yaml_get_str(first, "symbol"), "black_tex") == 0, "first symbol");

    const GCYamlNode* mods = gc_yaml_find(r, "modules");
    CHECK(mods && seq_count(mods) == 1, "modules");
    const GCYamlNode* mex = gc_yaml_find(mods->children, "extract");
    CHECK(seq_count(mex) == 1, "module extract");

    gc_yaml_free(r);
}

static void test_quoted_and_escape(void) {
    GCYamlNode* r = gc_yaml_parse_file("test/fixtures/quoted.yml");
    CHECK(r != NULL, "quoted parse");
    CHECK(strcmp(gc_yaml_get_str(r, "a"), "line1\nline2") == 0, "double-quoted escape");
    CHECK(strcmp(gc_yaml_get_str(r, "b"), "literal\\n") == 0, "single-quoted literal");
    CHECK(strcmp(gc_yaml_get_str(r, "c"), "plain string with spaces") == 0, "plain");
    CHECK(strcmp(gc_yaml_get_str(r, "d"), "A") == 0, "unicode escape");
    gc_yaml_free(r);
}

static void test_block_scalars(void) {
    GCYamlNode* r = gc_yaml_parse_file("test/fixtures/block.yml");
    CHECK(r != NULL, "block parse");
    const char* lit = gc_yaml_get_str(r, "literal");
    CHECK(lit && strcmp(lit, "line1\nline2\n") == 0, "literal");
    const char* fld = gc_yaml_get_str(r, "folded");
    CHECK(fld && strcmp(fld, "one two three\n") == 0, "folded");
    gc_yaml_free(r);
}

static void test_flow(void) {
    GCYamlNode* r = gc_yaml_parse_file("test/fixtures/flow.yml");
    CHECK(r != NULL, "flow parse");
    const GCYamlNode* l = gc_yaml_find(r, "list");
    CHECK(l && seq_count(l) == 3, "flow seq count");
    CHECK(strcmp(l->children->value, "a") == 0, "first flow item");
    const GCYamlNode* m = gc_yaml_find(r, "map");
    CHECK(m && strcmp(gc_yaml_get_str(m, "x"), "1") == 0, "flow mapping x");
    const GCYamlNode* n = gc_yaml_find(r, "nested");
    CHECK(n && seq_count(n) == 2, "nested top");
    CHECK(seq_count(n->children) == 2, "nested inner");
    gc_yaml_free(r);
}

static void test_anchors(void) {
    GCYamlNode* r = gc_yaml_parse_file("test/fixtures/anchors.yml");
    CHECK(r != NULL, "anchor parse");
    gc_yaml_free(r);
}

int main(void) {
    test_basic();
    test_quoted_and_escape();
    test_block_scalars();
    test_flow();
    test_anchors();
    if (fail) { fprintf(stderr, "%d failure(s)\n", fail); return 1; }
    printf("ok\n");
    return 0;
}
