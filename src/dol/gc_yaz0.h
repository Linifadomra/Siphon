#ifndef GC_YAZ0_H
#define GC_YAZ0_H

#include <stdint.h>
#include <stddef.h>

int gc_yaz0_is_compressed(const uint8_t* bytes, size_t n);

// out_data is malloc'd; caller frees.
int gc_yaz0_decompress(const uint8_t* in, size_t in_n,
                       uint8_t** out_data, size_t* out_n);

#endif
