#ifndef GC_YAML_H
#define GC_YAML_H

#include <stddef.h>

// Generic YAML tree built on top of libyaml.
//   - Mapping nodes have children with non-NULL key.
//   - Sequence nodes have children with NULL key.
//   - Scalar nodes have non-NULL value and no children.
typedef enum {
    GC_YAML_SCALAR = 0,
    GC_YAML_MAPPING,
    GC_YAML_SEQUENCE,
} GCYamlKind;

typedef struct GCYamlNode {
    GCYamlKind         kind;
    char*              key;       // NULL inside sequences
    char*              value;     // non-NULL on scalars
    struct GCYamlNode* children;
    struct GCYamlNode* next;
} GCYamlNode;

GCYamlNode* gc_yaml_parse_file(const char* path);
void gc_yaml_free(GCYamlNode* node);

const GCYamlNode* gc_yaml_find(const GCYamlNode* parent, const char* key);
const char* gc_yaml_get_str(const GCYamlNode* parent, const char* key);
int gc_yaml_seq_count(const GCYamlNode* node);

#endif
