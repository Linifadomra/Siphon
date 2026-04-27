#ifndef GC_JSON_H
#define GC_JSON_H

#include <stdio.h>

typedef struct {
    FILE* f;
    int   depth;
    int   need_comma;
} GCJsonWriter;

void gc_json_open(GCJsonWriter* w, FILE* f);
void gc_json_begin_object(GCJsonWriter* w);
void gc_json_end_object(GCJsonWriter* w);
void gc_json_begin_array(GCJsonWriter* w);
void gc_json_end_array(GCJsonWriter* w);
void gc_json_key(GCJsonWriter* w, const char* key);
void gc_json_kv_str(GCJsonWriter* w, const char* key, const char* val);
void gc_json_kv_int(GCJsonWriter* w, const char* key, long val);
void gc_json_kv_bool(GCJsonWriter* w, const char* key, int val);

#endif
