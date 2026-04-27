#include "gc_json.h"
#include <string.h>

static void indent(GCJsonWriter* w) {
    for (int i = 0; i < w->depth; i++) fputs("  ", w->f);
}

void gc_json_open(GCJsonWriter* w, FILE* f) {
    w->f = f;
    w->depth = 0;
    w->need_comma = 0;
}

static void emit_punct(GCJsonWriter* w) {
    if (w->need_comma) {
        fputc(',', w->f);
        fputc('\n', w->f);
    } else if (w->depth > 0) {
        fputc('\n', w->f);
    }
    indent(w);
}

void gc_json_begin_object(GCJsonWriter* w) {
    emit_punct(w);
    fputc('{', w->f);
    w->depth++;
    w->need_comma = 0;
}

void gc_json_end_object(GCJsonWriter* w) {
    w->depth--;
    fputc('\n', w->f);
    indent(w);
    fputc('}', w->f);
    w->need_comma = 1;
}

void gc_json_begin_array(GCJsonWriter* w) {
    emit_punct(w);
    fputc('[', w->f);
    w->depth++;
    w->need_comma = 0;
}

void gc_json_end_array(GCJsonWriter* w) {
    w->depth--;
    fputc('\n', w->f);
    indent(w);
    fputc(']', w->f);
    w->need_comma = 1;
}

static void emit_escaped(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f);  break;
            case '\f': fputs("\\f", f);  break;
            case '\n': fputs("\\n", f);  break;
            case '\r': fputs("\\r", f);  break;
            case '\t': fputs("\\t", f);  break;
            default:
                if (c < 0x20) fprintf(f, "\\u%04x", c);
                else          fputc(c, f);
        }
    }
    fputc('"', f);
}

void gc_json_key(GCJsonWriter* w, const char* key) {
    if (w->need_comma) {
        fputc(',', w->f);
    }
    fputc('\n', w->f);
    indent(w);
    emit_escaped(w->f, key);
    fputs(": ", w->f);
    w->need_comma = 0;
}

void gc_json_kv_str(GCJsonWriter* w, const char* key, const char* val) {
    gc_json_key(w, key);
    emit_escaped(w->f, val);
    w->need_comma = 1;
}

void gc_json_kv_int(GCJsonWriter* w, const char* key, long val) {
    gc_json_key(w, key);
    fprintf(w->f, "%ld", val);
    w->need_comma = 1;
}

void gc_json_kv_bool(GCJsonWriter* w, const char* key, int val) {
    gc_json_key(w, key);
    fputs(val ? "true" : "false", w->f);
    w->need_comma = 1;
}
