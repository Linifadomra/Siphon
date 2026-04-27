#ifndef GC_RVZ_H
#define GC_RVZ_H

#include <stddef.h>
#include <stdint.h>

// Expand an RVZ packed stream (post-outer-decompression) into disc bytes.
// data_offset is the group's disc position, used to phase the junk PRNG.
// Returns 0 on success, -1 on truncated input or output overflow.
int gc_rvz_unpack(const uint8_t* packed, size_t packed_size,
                  uint64_t data_offset,
                  uint8_t* out, size_t out_cap, size_t* out_len);

#endif
