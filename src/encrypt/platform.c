#include "platform.h"
#include <string.h>

#if defined(SIPHON_USE_COMMONCRYPTO)
#include <CommonCrypto/CommonCrypto.h>

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len) {
    size_t outLen;
    CCCrypt(kCCDecrypt, kCCAlgorithmAES128, 0,
            key, kCCKeySizeAES128, iv,
            in, len, out, len, &outLen);
}

#elif defined(SIPHON_USE_AESNI)
#include <wmmintrin.h>

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len) {
    __m128i rk[11];
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    rk[0] = k;
    #define EXP(i, imm) { \
        __m128i t = _mm_aeskeygenassist_si128(rk[i-1], imm); \
        t = _mm_shuffle_epi32(t, 0xFF); \
        __m128i s = _mm_xor_si128(rk[i-1], _mm_slli_si128(rk[i-1], 4)); \
        s = _mm_xor_si128(s, _mm_slli_si128(s, 4)); \
        s = _mm_xor_si128(s, _mm_slli_si128(s, 4)); \
        rk[i] = _mm_xor_si128(s, t); }
    EXP(1,0x01) EXP(2,0x02) EXP(3,0x04) EXP(4,0x08)
    EXP(5,0x10) EXP(6,0x20) EXP(7,0x40) EXP(8,0x80)
    EXP(9,0x1b) EXP(10,0x36)
    #undef EXP

    __m128i drk[11];
    drk[0] = rk[10];
    for (int i = 1; i < 10; i++) drk[i] = _mm_aesimc_si128(rk[10-i]);
    drk[10] = rk[0];

    __m128i feedback = _mm_loadu_si128((const __m128i*)iv);
    for (size_t i = 0; i < len; i += 16) {
        __m128i ct = _mm_loadu_si128((const __m128i*)(in + i));
        __m128i pt = _mm_xor_si128(ct, drk[0]);
        for (int r = 1; r < 10; r++) pt = _mm_aesdec_si128(pt, drk[r]);
        pt = _mm_aesdeclast_si128(pt, drk[10]);
        pt = _mm_xor_si128(pt, feedback);
        _mm_storeu_si128((__m128i*)(out + i), pt);
        feedback = ct;
    }
}

#elif defined(SIPHON_USE_ARM_CRYPTO)
#include <arm_neon.h>
#include "aes.h"

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t rkbuf[176];
    struct AES_ctx ctx;
    AES_init_ctx_key(&ctx, key);
    memcpy(rkbuf, ctx.RoundKey, 176);

    uint8x16_t rk[11];
    for (int i = 0; i < 11; i++) rk[i] = vld1q_u8(rkbuf + i * 16);

    uint8x16_t feedback = vld1q_u8(iv);
    for (size_t i = 0; i < len; i += 16) {
        uint8x16_t ct = vld1q_u8(in + i);
        uint8x16_t pt = veorq_u8(ct, rk[10]);
        for (int r = 9; r > 0; r--) {
            pt = vaesdq_u8(pt, vdupq_n_u8(0));
            pt = vaesimcq_u8(pt);
            pt = veorq_u8(pt, rk[r]);
        }
        pt = vaesdq_u8(pt, vdupq_n_u8(0));
        pt = veorq_u8(pt, rk[0]);
        pt = veorq_u8(pt, feedback);
        vst1q_u8(out + i, pt);
        feedback = ct;
    }
}

#else
#include "aes.h"

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len) {
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    if (out != in) memcpy(out, in, len);
    AES_CBC_decrypt_buffer(&ctx, out, len);
}
#endif