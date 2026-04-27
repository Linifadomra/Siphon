#ifndef GC_SYMBOLS_H
#define GC_SYMBOLS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char*    name;
    char*    section;
    uint32_t address;
    uint32_t size;
} GCSymbol;

typedef struct {
    GCSymbol* items;
    int       count;
    int       cap;
} GCSymbolTable;

int gc_symbols_load(GCSymbolTable* tbl, const char* path);
const GCSymbol* gc_symbols_find(const GCSymbolTable* tbl, const char* name);
void gc_symbols_free(GCSymbolTable* tbl);

#endif
