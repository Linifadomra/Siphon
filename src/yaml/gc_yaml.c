#include "gc_yaml.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* dup_cstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static GCYamlNode* new_node(GCYamlKind kind) {
    GCYamlNode* n = (GCYamlNode*)calloc(1, sizeof(GCYamlNode));
    if (n) n->kind = kind;
    return n;
}

void gc_yaml_free(GCYamlNode* node) {
    while (node) {
        GCYamlNode* next = node->next;
        gc_yaml_free(node->children);
        free(node->key);
        free(node->value);
        free(node);
        node = next;
    }
}

static void append(GCYamlNode* parent, GCYamlNode* child) {
    if (!parent->children) { parent->children = child; return; }
    GCYamlNode* tail = parent->children;
    while (tail->next) tail = tail->next;
    tail->next = child;
}

static int build_body(yaml_parser_t* p, GCYamlNode* into);

// Consume the content represented by `ev` and attach it to `parent` under
// optional `key`. Takes ownership of `key` and `ev`.
static int consume_event(yaml_parser_t* p, GCYamlNode* parent, char* key, yaml_event_t* ev) {
    int rc = 1;
    switch (ev->type) {
        case YAML_SCALAR_EVENT: {
            GCYamlNode* n = new_node(GC_YAML_SCALAR);
            if (!n) { free(key); rc = 0; break; }
            n->key = key;
            n->value = dup_cstr((const char*)ev->data.scalar.value);
            append(parent, n);
            break;
        }
        case YAML_SEQUENCE_START_EVENT: {
            GCYamlNode* n = new_node(GC_YAML_SEQUENCE);
            if (!n) { free(key); rc = 0; break; }
            n->key = key;
            append(parent, n);
            rc = build_body(p, n);
            break;
        }
        case YAML_MAPPING_START_EVENT: {
            GCYamlNode* n = new_node(GC_YAML_MAPPING);
            if (!n) { free(key); rc = 0; break; }
            n->key = key;
            append(parent, n);
            rc = build_body(p, n);
            break;
        }
        case YAML_ALIAS_EVENT: {
            // Anchors aren't resolved; alias becomes an empty scalar.
            GCYamlNode* n = new_node(GC_YAML_SCALAR);
            if (!n) { free(key); rc = 0; break; }
            n->key = key;
            n->value = dup_cstr("");
            append(parent, n);
            break;
        }
        default:
            free(key);
            rc = 0;
            break;
    }
    yaml_event_delete(ev);
    return rc;
}

static int build_body(yaml_parser_t* p, GCYamlNode* into) {
    int is_map = (into->kind == GC_YAML_MAPPING);
    yaml_event_type_t end_type = is_map ? YAML_MAPPING_END_EVENT : YAML_SEQUENCE_END_EVENT;
    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(p, &ev)) return 0;
        if (ev.type == end_type) {
            yaml_event_delete(&ev);
            return 1;
        }
        char* key = NULL;
        if (is_map) {
            if (ev.type != YAML_SCALAR_EVENT) {
                yaml_event_delete(&ev);
                return 0;
            }
            key = dup_cstr((const char*)ev.data.scalar.value);
            yaml_event_delete(&ev);
            if (!key) return 0;
            if (!yaml_parser_parse(p, &ev)) { free(key); return 0; }
        }
        if (!consume_event(p, into, key, &ev)) return 0;
    }
}

GCYamlNode* gc_yaml_parse_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "siphon: cannot open %s\n", path);
        return NULL;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(f);
        fprintf(stderr, "siphon: yaml_parser_initialize failed\n");
        return NULL;
    }
    yaml_parser_set_input_file(&parser, f);

    GCYamlNode* root = new_node(GC_YAML_MAPPING);
    if (!root) {
        yaml_parser_delete(&parser);
        fclose(f);
        return NULL;
    }

    int ok = 1;
    int started = 0;
    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(&parser, &ev)) { ok = 0; break; }
        yaml_event_type_t type = ev.type;
        if (type == YAML_STREAM_START_EVENT || type == YAML_DOCUMENT_START_EVENT) {
            yaml_event_delete(&ev);
            continue;
        }
        if (type == YAML_DOCUMENT_END_EVENT) {
            yaml_event_delete(&ev);
            continue;
        }
        if (type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (started) {
            // Multi-document streams: only the first populates the tree.
            yaml_event_delete(&ev);
            continue;
        }
        started = 1;
        if (type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            if (!build_body(&parser, root)) { ok = 0; break; }
        } else if (type == YAML_SEQUENCE_START_EVENT) {
            root->kind = GC_YAML_SEQUENCE;
            yaml_event_delete(&ev);
            if (!build_body(&parser, root)) { ok = 0; break; }
        } else if (type == YAML_SCALAR_EVENT) {
            root->kind = GC_YAML_SCALAR;
            root->value = dup_cstr((const char*)ev.data.scalar.value);
            yaml_event_delete(&ev);
        } else {
            yaml_event_delete(&ev);
            ok = 0;
            break;
        }
    }

    if (!ok) {
        fprintf(stderr, "siphon: yaml parse error at line %lu col %lu: %s\n",
            (unsigned long)parser.problem_mark.line + 1,
            (unsigned long)parser.problem_mark.column + 1,
            parser.problem ? parser.problem : "(unknown)");
        gc_yaml_free(root);
        root = NULL;
    }

    yaml_parser_delete(&parser);
    fclose(f);
    return root;
}

const GCYamlNode* gc_yaml_find(const GCYamlNode* parent, const char* key) {
    if (!parent) return NULL;
    for (const GCYamlNode* c = parent->children; c; c = c->next) {
        if (c->key && strcmp(c->key, key) == 0) return c;
    }
    return NULL;
}

const char* gc_yaml_get_str(const GCYamlNode* parent, const char* key) {
    const GCYamlNode* n = gc_yaml_find(parent, key);
    return (n && n->kind == GC_YAML_SCALAR) ? n->value : NULL;
}

int gc_yaml_seq_count(const GCYamlNode* node) {
    if (!node) return 0;
    int n = 0;
    for (const GCYamlNode* c = node->children; c; c = c->next) n++;
    return n;
}
