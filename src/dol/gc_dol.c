#define _POSIX_C_SOURCE 200809L
#include "siphon_log.h"
#include "gc_dol.h"
#include "gc_yaml.h"
#include "gc_symbols.h"
#include "gc_json.h"
#include "gc_yaz0.h"
#include "gc_disc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "macros.h"

#define CONFIG_VERSION "1.8.0"

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int mkpath(const char* path) {
    char buf[1024];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];
            buf[i] = '\0';
            if (MKDIR_ONE(buf) != 0 && errno != EEXIST) {
                buf[i] = c;
                return -1;
            }
            buf[i] = c;
        }
    }
    if (MKDIR_ONE(buf) != 0 && errno != EEXIST) return -1;
    return 0;
}

static uint8_t* slurp(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static int file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && IS_REG_FILE(st);
}

typedef struct {
    uint32_t file_off;
    uint32_t addr;
    uint32_t size;
} DolSection;

typedef struct {
    uint8_t*   data;
    size_t     size;
    DolSection sections[18];
} DolFile;

static int dol_load(DolFile* d, const char* path) {
    memset(d, 0, sizeof(*d));
    d->data = slurp(path, &d->size);
    if (!d->data) {
        siphon_log("cannot read DOL %s", path);
        return -1;
    }
    if (d->size < 0x100) {
        siphon_log("DOL %s truncated", path);
        return -1;
    }
    for (int i = 0; i < 18; i++) {
        d->sections[i].file_off = be32(d->data + 0x00 + i * 4);
        d->sections[i].addr     = be32(d->data + 0x48 + i * 4);
        d->sections[i].size     = be32(d->data + 0x90 + i * 4);
    }
    return 0;
}

static void dol_free(DolFile* d) {
    free(d->data);
    d->data = NULL;
    d->size = 0;
}

static int dol_va_to_file(const DolFile* d, uint32_t va, uint32_t size, uint32_t* file_off) {
    for (int i = 0; i < 18; i++) {
        const DolSection* s = &d->sections[i];
        if (s->size == 0) continue;
        if (va >= s->addr && va + size <= s->addr + s->size) {
            *file_off = s->file_off + (va - s->addr);
            return 0;
        }
    }
    return -1;
}

typedef struct {
    uint32_t file_off;
    uint32_t size;
    char*    name;
} RelSection;

typedef struct {
    uint8_t*    data;
    size_t      size;
    RelSection* sections;
    int         section_count;
    char**      section_names;
    int         section_name_count;
} RelFile;

static int parse_splits_sections(const char* path, char*** out_names, int* out_count) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        siphon_log("cannot open %s", path);
        return -1;
    }
    char** names = NULL;
    int count = 0, cap = 0;
    char line[1024];
    int in_sections = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';

        if (!in_sections) {
            const char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "Sections:", 9) == 0) in_sections = 1;
            continue;
        }
        if (line[0] != '\t' && line[0] != ' ') break;
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;
        const char* name_end = p;
        while (*name_end && *name_end != ' ' && *name_end != '\t') name_end++;
        if (count + 1 > cap) {
            cap = cap ? cap * 2 : 16;
            names = (char**)realloc(names, cap * sizeof(char*));
        }
        size_t l = (size_t)(name_end - p);
        char* nm = (char*)malloc(l + 1);
        memcpy(nm, p, l);
        nm[l] = '\0';
        names[count++] = nm;
    }
    fclose(f);
    *out_names = names;
    *out_count = count;
    return 0;
}

static int rel_load(RelFile* r, const uint8_t* bytes, size_t n, const char* splits_path) {
    memset(r, 0, sizeof(*r));
    if (n < 0x40) {
        siphon_log("REL header too small (%zu bytes from %s)", n, splits_path);
        return -1;
    }
    r->data = (uint8_t*)malloc(n);
    memcpy(r->data, bytes, n);
    r->size = n;

    uint32_t num_sections = be32(r->data + 0x0C);
    uint32_t section_info_off = be32(r->data + 0x10);
    if (section_info_off + num_sections * 8 > n) {
        siphon_log("REL section info out of range (off=0x%x num=%u size=%zu) from %s",
                section_info_off, num_sections, n, splits_path);
        return -1;
    }

    r->section_count = (int)num_sections;
    r->sections = (RelSection*)calloc(num_sections, sizeof(RelSection));
    for (uint32_t i = 0; i < num_sections; i++) {
        uint32_t off = be32(r->data + section_info_off + i * 8);
        uint32_t len = be32(r->data + section_info_off + i * 8 + 4);
        r->sections[i].file_off = off & ~1u;
        r->sections[i].size     = len;
    }

    if (splits_path) {
        if (parse_splits_sections(splits_path, &r->section_names, &r->section_name_count) != 0) {
            return -1;
        }
        // splits.txt lists section names in declared order, starting at REL
        // section index 1 (index 0 is the REL placeholder slot). Skip empty
        // slots in the REL since splits never names them.
        int name_idx = 0;
        for (uint32_t i = 1; i < num_sections && name_idx < r->section_name_count; i++) {
            if (r->sections[i].size == 0 && r->sections[i].file_off == 0) continue;
            r->sections[i].name = r->section_names[name_idx++];
        }
    }
    return 0;
}

static void rel_free(RelFile* r) {
    free(r->data);
    free(r->sections);
    for (int i = 0; i < r->section_name_count; i++) free(r->section_names[i]);
    free(r->section_names);
    memset(r, 0, sizeof(*r));
}

static int rel_resolve(const RelFile* r, const char* section_name,
                       uint32_t off, uint32_t size, uint32_t* file_off) {
    for (int i = 0; i < r->section_count; i++) {
        const RelSection* s = &r->sections[i];
        if (!s->name) continue;
        if (strcmp(s->name, section_name) != 0) continue;
        if (off + size > s->size) return -1;
        *file_off = s->file_off + off;
        return 0;
    }
    return -1;
}

typedef struct {
    char** items;
    int    count;
    int    cap;
} StringList;

static void sl_push(StringList* sl, const char* s) {
    if (sl->count + 1 > sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 16;
        sl->items = (char**)realloc(sl->items, sl->cap * sizeof(char*));
    }
    sl->items[sl->count++] = strdup(s);
}

static void sl_free(StringList* sl) {
    for (int i = 0; i < sl->count; i++) free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

static int write_bytes(const char* out_dir, const char* binary_rel,
                       const uint8_t* data, size_t n) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/bin/%s", out_dir, binary_rel);
    char* slash = strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        if (mkpath(path) != 0) return -1;
        *slash = '/';
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        siphon_log("cannot open %s for write", path);
        return -1;
    }
    if (fwrite(data, 1, n, f) != n) {
        fclose(f);
        siphon_log("short write to %s", path);
        return -1;
    }
    fclose(f);
    return 0;
}

// Cache one open archive across many read_rel calls. Hundreds of modules
// often pull from the same RELS.arc; reopening + reparsing each time is
// the difference between sub-second and minutes.
typedef struct {
    char    path[1024];
    GCArc*  arc;
} ArcCache;

static GCArc* arc_cache_get(ArcCache* cache, const char* path) {
    if (cache->arc && strcmp(cache->path, path) == 0) return cache->arc;
    if (cache->arc) gc_arc_close(cache->arc);
    cache->arc = gc_arc_open_file(path);
    snprintf(cache->path, sizeof(cache->path), "%s", path);
    return cache->arc;
}

static void arc_cache_free(ArcCache* cache) {
    if (cache->arc) gc_arc_close(cache->arc);
    cache->arc = NULL;
    cache->path[0] = '\0';
}

// Locate a REL given a dtk-template "object" path like
// "files/RELS.arc:rels/mmem/foo.rel". Accepts either an actual archive on
// disk or a pre-extracted directory with the .arc suffix dropped.
static int read_rel(ArcCache* cache, const char* base,
                    const char* arc_rel_path, const char* inner_path,
                    uint8_t** out_data, size_t* out_size, char* dep_path, size_t dep_n) {
    char arc_full[1024];
    snprintf(arc_full, sizeof(arc_full), "%s/%s", base, arc_rel_path);

    char dir_form[1024];
    {
        size_t alen = strlen(arc_rel_path);
        const char* dot = strrchr(arc_rel_path, '.');
        size_t prefix_len = (dot && strcmp(dot, ".arc") == 0) ? (size_t)(dot - arc_rel_path) : alen;
        snprintf(dir_form, sizeof(dir_form), "%s/%.*s/%s", base,
                 (int)prefix_len, arc_rel_path, inner_path);
    }

    if (file_exists(dir_form)) {
        *out_data = slurp(dir_form, out_size);
        if (!*out_data) return -1;
        snprintf(dep_path, dep_n, "%s", dir_form);
        return 0;
    }

    if (file_exists(arc_full)) {
        GCArc* arc = arc_cache_get(cache, arc_full);
        if (!arc) {
            siphon_log("cannot open archive %s", arc_full);
            return -1;
        }
        // gc_arc strips the archive's root dir from entry names, so a config
        // path like "rels/mmem/foo.rel" needs both forms tried. Case-insensitive
        // because dtk-style configs sometimes diverge from the RARC string table.
        const char* slash = strchr(inner_path, '/');
        const char* alt = slash ? slash + 1 : inner_path;
        int n = gc_arc_entry_count(arc);
        int found = -1;
        for (int i = 0; i < n; i++) {
            const GCEntry* e = gc_arc_entry(arc, i);
            if (!e || !e->name || e->type != GC_ENTRY_FILE) continue;
            if (strcasecmp(e->name, inner_path) == 0 || strcasecmp(e->name, alt) == 0) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            siphon_log("%s not found inside %s", inner_path, arc_full);
            return -1;
        }
        if (gc_arc_read_file(arc, found, (void**)out_data, out_size) != 0) {
            siphon_log("cannot read entry from %s", arc_full);
            return -1;
        }
        snprintf(dep_path, dep_n, "%s", arc_full);
        return 0;
    }

    siphon_log("cannot find %s (tried %s and %s)",
            inner_path, dir_form, arc_full);
    return -1;
}

static int maybe_decompress_yaz0(uint8_t** pp, size_t* pn) {
    if (!gc_yaz0_is_compressed(*pp, *pn)) return 0;
    uint8_t* out = NULL;
    size_t   out_n = 0;
    if (gc_yaz0_decompress(*pp, *pn, &out, &out_n) != 0) {
        siphon_log("yaz0 decompress failed");
        return -1;
    }
    free(*pp);
    *pp = out;
    *pn = out_n;
    return 0;
}

typedef int (*resolve_fn)(void* ctx, const char* sym_name, uint32_t* file_off, uint32_t* size);

typedef struct {
    const GCSymbolTable* syms;
    const DolFile*       dol;
    const RelFile*       rel;
} ResolveCtx;

static int resolve_dol(void* ctx, const char* name, uint32_t* file_off, uint32_t* size) {
    ResolveCtx* c = (ResolveCtx*)ctx;
    const GCSymbol* s = gc_symbols_find(c->syms, name);
    if (!s) {
        siphon_log("symbol %s not found in DOL symbols", name);
        return -1;
    }
    if (s->size == 0) {
        siphon_log("symbol %s has zero size", name);
        return -1;
    }
    if (dol_va_to_file(c->dol, s->address, s->size, file_off) != 0) {
        siphon_log("symbol %s @ 0x%08x not in any DOL section", name, s->address);
        return -1;
    }
    *size = s->size;
    return 0;
}

static int resolve_rel(void* ctx, const char* name, uint32_t* file_off, uint32_t* size) {
    ResolveCtx* c = (ResolveCtx*)ctx;
    const GCSymbol* s = gc_symbols_find(c->syms, name);
    if (!s) {
        siphon_log("symbol %s not found in REL symbols", name);
        return -1;
    }
    if (s->size == 0) {
        siphon_log("symbol %s has zero size", name);
        return -1;
    }
    if (rel_resolve(c->rel, s->section, s->address, s->size, file_off) != 0) {
        siphon_log("symbol %s in section %s not in REL", name, s->section);
        return -1;
    }
    *size = s->size;
    return 0;
}

static int process_extracts(const GCYamlNode* extract_node, const char* out_dir,
                            const uint8_t* base_data,
                            void* ctx, resolve_fn fn,
                            GCJsonWriter* jw) {
    if (!extract_node) return 0;
    for (const GCYamlNode* e = extract_node->children; e; e = e->next) {
        const char* sym = gc_yaml_get_str(e, "symbol");
        const char* bin = gc_yaml_get_str(e, "binary");
        const char* hdr = gc_yaml_get_str(e, "header");
        if (!sym || !bin || !hdr) continue;

        uint32_t off = 0, sz = 0;
        if (fn(ctx, sym, &off, &sz) != 0) return -1;
        if (write_bytes(out_dir, bin, base_data + off, sz) != 0) return -1;

        gc_json_begin_object(jw);
        gc_json_kv_str(jw, "symbol", sym);
        gc_json_kv_str(jw, "binary", bin);
        gc_json_kv_str(jw, "header", hdr);
        const char* htype = gc_yaml_get_str(e, "header_type");
        if (htype) gc_json_kv_str(jw, "header_type", htype);
        const char* ctype = gc_yaml_get_str(e, "custom_type");
        if (ctype) gc_json_kv_str(jw, "custom_type", ctype);
        gc_json_end_object(jw);
    }
    return 0;
}

static int split_rel_path(const char* full,
                          char* arc_out, size_t arc_n, char* inner_out, size_t inner_n) {
    const char* colon = strchr(full, ':');
    if (!colon) return -1;
    snprintf(arc_out, arc_n, "%.*s", (int)(colon - full), full);
    snprintf(inner_out, inner_n, "%s", colon + 1);
    return 0;
}

int gc_dol_split(const char* yaml_path, const char* out_dir) {
    GCYamlNode* cfg = gc_yaml_parse_file(yaml_path);
    if (!cfg) return -1;

    const char* object_base = gc_yaml_get_str(cfg, "object_base");
    const char* object_rel  = gc_yaml_get_str(cfg, "object");
    const char* sym_rel     = gc_yaml_get_str(cfg, "symbols");
    if (!object_base || !object_rel || !sym_rel) {
        siphon_log("config missing object_base/object/symbols");
        gc_yaml_free(cfg);
        return -1;
    }

    char dol_path[1024];
    snprintf(dol_path, sizeof(dol_path), "%s/%s", object_base, object_rel);

    DolFile dol;
    if (dol_load(&dol, dol_path) != 0) { gc_yaml_free(cfg); return -1; }

    GCSymbolTable dol_syms;
    if (gc_symbols_load(&dol_syms, sym_rel) != 0) {
        dol_free(&dol); gc_yaml_free(cfg); return -1;
    }

    StringList deps = {0};
    ArcCache arc_cache = {{0}, NULL};
    sl_push(&deps, yaml_path);
    sl_push(&deps, dol_path);
    sl_push(&deps, sym_rel);

    if (mkpath(out_dir) != 0) {
        siphon_log("cannot mkdir %s", out_dir);
        gc_symbols_free(&dol_syms); dol_free(&dol); gc_yaml_free(cfg);
        sl_free(&deps);
        return -1;
    }

    char json_path[1024];
    snprintf(json_path, sizeof(json_path), "%s/config.json", out_dir);
    FILE* jf = fopen(json_path, "wb");
    if (!jf) {
        siphon_log("cannot open %s for write", json_path);
        gc_symbols_free(&dol_syms); dol_free(&dol); gc_yaml_free(cfg);
        sl_free(&deps);
        return -1;
    }

    const char* top_name = gc_yaml_get_str(cfg, "name");
    if (!top_name) top_name = "framework";
    char top_ldscript[1024];
    snprintf(top_ldscript, sizeof(top_ldscript), "%s/ldscript.lcf", out_dir);

    GCJsonWriter jw;
    gc_json_open(&jw, jf);
    gc_json_begin_object(&jw);
    gc_json_kv_str(&jw, "version", CONFIG_VERSION);
    gc_json_kv_str(&jw, "name", top_name);
    gc_json_kv_int(&jw, "module_id", 0);
    gc_json_kv_str(&jw, "ldscript", top_ldscript);
    gc_json_kv_str(&jw, "entry", "__start");

    gc_json_key(&jw, "links");
    gc_json_begin_array(&jw);
    gc_json_end_array(&jw);

    gc_json_key(&jw, "units");
    gc_json_begin_array(&jw);
    gc_json_end_array(&jw);

    gc_json_key(&jw, "extract");
    gc_json_begin_array(&jw);
    {
        ResolveCtx ctx = { .syms = &dol_syms, .dol = &dol, .rel = NULL };
        const GCYamlNode* ex = gc_yaml_find(cfg, "extract");
        if (process_extracts(ex, out_dir, dol.data, &ctx, resolve_dol, &jw) != 0) {
            gc_json_end_array(&jw);
            gc_json_end_object(&jw);
            fclose(jf);
            gc_symbols_free(&dol_syms); dol_free(&dol); gc_yaml_free(cfg);
            sl_free(&deps); arc_cache_free(&arc_cache);
            return -1;
        }
    }
    gc_json_end_array(&jw);

    gc_json_key(&jw, "modules");
    gc_json_begin_array(&jw);

    const GCYamlNode* mods = gc_yaml_find(cfg, "modules");
    if (mods) {
        for (const GCYamlNode* m = mods->children; m; m = m->next) {
            const char* mobj  = gc_yaml_get_str(m, "object");
            const char* msym  = gc_yaml_get_str(m, "symbols");
            const char* msplt = gc_yaml_get_str(m, "splits");
            const GCYamlNode* mext = gc_yaml_find(m, "extract");

            char mod_name[256] = "module";
            if (mobj) {
                const char* slash = strrchr(mobj, '/');
                const char* base = slash ? slash + 1 : mobj;
                size_t bl = strlen(base);
                if (bl > 4 && strcmp(base + bl - 4, ".rel") == 0) bl -= 4;
                if (bl >= sizeof(mod_name)) bl = sizeof(mod_name) - 1;
                memcpy(mod_name, base, bl);
                mod_name[bl] = '\0';
            }

            char mod_ldscript[1024];
            snprintf(mod_ldscript, sizeof(mod_ldscript), "%s/%s/ldscript.lcf", out_dir, mod_name);

            // Always read the REL so we can pull module_id from its header,
            // even if the module has no extract entries to process.
            uint8_t* rel_bytes = NULL;
            size_t   rel_n = 0;
            char     rel_dep[1024] = "";
            uint32_t module_id_val = 0;
            int      rel_ok = 0;
            if (mobj) {
                const char* colon = strchr(mobj, ':');
                if (colon) {
                    char arc_rel[1024], inner[1024];
                    split_rel_path(mobj, arc_rel, sizeof(arc_rel), inner, sizeof(inner));
                    rel_ok = (read_rel(&arc_cache, object_base, arc_rel, inner,
                                       &rel_bytes, &rel_n, rel_dep, sizeof(rel_dep)) == 0);
                } else {
                    char direct[1024];
                    snprintf(direct, sizeof(direct), "%s/%s", object_base, mobj);
                    rel_bytes = slurp(direct, &rel_n);
                    if (rel_bytes) {
                        snprintf(rel_dep, sizeof(rel_dep), "%s", direct);
                        rel_ok = 1;
                    }
                }
                if (rel_ok && maybe_decompress_yaz0(&rel_bytes, &rel_n) != 0) rel_ok = 0;
                if (rel_ok && rel_n >= 4) module_id_val = be32(rel_bytes);
                if (rel_ok) sl_push(&deps, rel_dep);
            }

            gc_json_begin_object(&jw);
            gc_json_kv_str(&jw, "name", mod_name);
            gc_json_kv_int(&jw, "module_id", (long)module_id_val);
            if (mobj) gc_json_kv_str(&jw, "object", mobj);
            if (gc_yaml_get_str(m, "hash")) gc_json_kv_str(&jw, "hash", gc_yaml_get_str(m, "hash"));
            gc_json_kv_str(&jw, "ldscript", mod_ldscript);
            gc_json_kv_str(&jw, "entry", "_prolog");

            // project.py refuses to emit a REL link rule with zero inputs unless
            // ProjectConfig.rel_empty_file is set, so give every module one stub
            // unit. The link rule never runs under ninja pre-compile.
            gc_json_key(&jw, "units");
            gc_json_begin_array(&jw);
            gc_json_begin_object(&jw);
            char stub_obj[1024];
            snprintf(stub_obj, sizeof(stub_obj), "%s/%s/%s.stub.o", out_dir, mod_name, mod_name);
            gc_json_kv_str(&jw, "object", stub_obj);
            gc_json_kv_str(&jw, "name", mod_name);
            gc_json_kv_bool(&jw, "autogenerated", 1);
            gc_json_end_object(&jw);
            gc_json_end_array(&jw);

            gc_json_key(&jw, "extract");
            gc_json_begin_array(&jw);
            if (rel_ok && mext && msym && msplt) {
                sl_push(&deps, msym);
                sl_push(&deps, msplt);
                RelFile rf;
                if (rel_load(&rf, rel_bytes, rel_n, msplt) == 0) {
                    GCSymbolTable rel_syms;
                    if (gc_symbols_load(&rel_syms, msym) == 0) {
                        ResolveCtx ctx = { .syms = &rel_syms, .dol = NULL, .rel = &rf };
                        process_extracts(mext, out_dir, rf.data, &ctx, resolve_rel, &jw);
                        gc_symbols_free(&rel_syms);
                    }
                    rel_free(&rf);
                }
            }
            free(rel_bytes);
            gc_json_end_array(&jw);
            gc_json_end_object(&jw);
        }
    }
    gc_json_end_array(&jw);
    gc_json_end_object(&jw);
    fputc('\n', jf);
    fclose(jf);

    char dep_path[1024];
    snprintf(dep_path, sizeof(dep_path), "%s/dep", out_dir);
    FILE* df = fopen(dep_path, "wb");
    if (df) {
        fprintf(df, "%s/config.json:", out_dir);
        for (int i = 0; i < deps.count; i++) {
            fprintf(df, " \\\n  %s", deps.items[i]);
        }
        fputc('\n', df);
        fclose(df);
    }

    sl_free(&deps);
    arc_cache_free(&arc_cache);
    gc_symbols_free(&dol_syms);
    dol_free(&dol);
    gc_yaml_free(cfg);
    return 0;
}
