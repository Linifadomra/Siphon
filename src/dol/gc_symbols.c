#include "gc_symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char* dup_n(const char* s, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int parse_hex_or_dec(const char* s, uint32_t* out) {
    if (!s || !*s) return 0;
    char* end = NULL;
    unsigned long v;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        v = strtoul(s + 2, &end, 16);
    } else {
        v = strtoul(s, &end, 10);
    }
    if (end == s) return 0;
    *out = (uint32_t)v;
    return 1;
}

static const char* find_meta(const char* s, const char* key) {
    size_t kl = strlen(key);
    while (*s) {
        const char* sp = s;
        while (*sp == ' ' || *sp == '\t') sp++;
        if (strncmp(sp, key, kl) == 0 && sp[kl] == ':') return sp + kl + 1;
        const char* nxt = strchr(s, ' ');
        if (!nxt) return NULL;
        s = nxt + 1;
    }
    return NULL;
}

static void ensure_cap(GCSymbolTable* tbl, int n) {
    if (n <= tbl->cap) return;
    int c = tbl->cap ? tbl->cap : 256;
    while (c < n) c *= 2;
    tbl->items = (GCSymbol*)realloc(tbl->items, c * sizeof(GCSymbol));
    tbl->cap = c;
}

int gc_symbols_load(GCSymbolTable* tbl, const char* path) {
    memset(tbl, 0, sizeof(*tbl));
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "siphon: cannot open %s\n", path);
        return -1;
    }

    // Lines look like: name = section:0xADDR; // type:X size:0xN scope:Y align:Z
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\r' || line[n-1] == '\n' || line[n-1] == ' ' || line[n-1] == '\t')) {
            line[--n] = '\0';
        }
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        const char* eq = strchr(p, '=');
        if (!eq) continue;
        const char* name_end = eq;
        while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        const char* colon = strchr(val, ':');
        if (!colon) continue;
        const char* sect_end = colon;
        const char* addr_start = colon + 1;
        const char* semi = strchr(addr_start, ';');
        const char* addr_end = semi ? semi : addr_start + strlen(addr_start);

        char addr_buf[32];
        size_t al = (size_t)(addr_end - addr_start);
        if (al >= sizeof(addr_buf)) al = sizeof(addr_buf) - 1;
        memcpy(addr_buf, addr_start, al);
        addr_buf[al] = '\0';
        while (al && (addr_buf[al-1] == ' ' || addr_buf[al-1] == '\t')) addr_buf[--al] = '\0';
        uint32_t addr = 0;
        if (!parse_hex_or_dec(addr_buf, &addr)) continue;

        uint32_t size = 0;
        const char* meta = semi ? strstr(semi, "//") : NULL;
        if (meta) {
            meta += 2;
            const char* sz = find_meta(meta, "size");
            if (sz) {
                while (*sz == ' ' || *sz == '\t') sz++;
                char num[32];
                int i = 0;
                while (sz[i] && sz[i] != ' ' && sz[i] != '\t' && i < (int)sizeof(num)-1) {
                    num[i] = sz[i];
                    i++;
                }
                num[i] = '\0';
                parse_hex_or_dec(num, &size);
            }
        }

        ensure_cap(tbl, tbl->count + 1);
        GCSymbol* s = &tbl->items[tbl->count++];
        s->name = dup_n(p, (size_t)(name_end - p));
        s->section = dup_n(val, (size_t)(sect_end - val));
        s->address = addr;
        s->size = size;
    }
    fclose(f);
    return 0;
}

const GCSymbol* gc_symbols_find(const GCSymbolTable* tbl, const char* name) {
    for (int i = 0; i < tbl->count; i++) {
        if (strcmp(tbl->items[i].name, name) == 0) return &tbl->items[i];
    }
    return NULL;
}

void gc_symbols_free(GCSymbolTable* tbl) {
    for (int i = 0; i < tbl->count; i++) {
        free(tbl->items[i].name);
        free(tbl->items[i].section);
    }
    free(tbl->items);
    tbl->items = NULL;
    tbl->count = 0;
    tbl->cap = 0;
}
