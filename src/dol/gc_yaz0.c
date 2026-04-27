#include "gc_yaz0.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gc_yaz0_is_compressed(const uint8_t* bytes, size_t n) {
    return n >= 4 && bytes[0] == 'Y' && bytes[1] == 'a' && bytes[2] == 'z' && bytes[3] == '0';
}

int gc_yaz0_decompress(const uint8_t* in, size_t in_n,
                       uint8_t** out_data, size_t* out_n) {
    if (in_n < 0x10 || !gc_yaz0_is_compressed(in, in_n)) return -1;

    uint32_t dec_size = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) |
                        ((uint32_t)in[6] << 8)  |  (uint32_t)in[7];
    uint8_t* out = (uint8_t*)malloc(dec_size);
    if (!out) return -1;

    size_t src = 0x10;
    size_t dst = 0;
    uint8_t code = 0;
    int code_bits = 0;

    while (dst < dec_size) {
        if (code_bits == 0) {
            if (src >= in_n) { free(out); return -1; }
            code = in[src++];
            code_bits = 8;
        }
        if (code & 0x80) {
            // literal
            if (src >= in_n || dst >= dec_size) { free(out); return -1; }
            out[dst++] = in[src++];
        } else {
            // back-reference
            if (src + 1 >= in_n) { free(out); return -1; }
            uint8_t b1 = in[src++];
            uint8_t b2 = in[src++];
            uint32_t dist = (((uint32_t)(b1 & 0x0F) << 8) | b2) + 1;
            uint32_t len  = (b1 >> 4);
            if (len == 0) {
                if (src >= in_n) { free(out); return -1; }
                len = (uint32_t)in[src++] + 0x12;
            } else {
                len += 2;
            }
            if (dist > dst) { free(out); return -1; }
            for (uint32_t i = 0; i < len; i++) {
                if (dst >= dec_size) { free(out); return -1; }
                out[dst] = out[dst - dist];
                dst++;
            }
        }
        code <<= 1;
        code_bits--;
    }

    *out_data = out;
    *out_n = dec_size;
    return 0;
}
