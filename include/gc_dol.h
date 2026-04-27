#ifndef GC_DOL_H
#define GC_DOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int gc_dol_split(const char* yaml_path, const char* out_dir);

#ifdef __cplusplus
}
#endif

#endif
