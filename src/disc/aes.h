#ifndef SIPHON_AES_H
#define SIPHON_AES_H
#include <stdint.h>
#include <stddef.h>
#define AES_BLOCKLEN 16
#define AES_keyExpSize 176
struct AES_ctx{uint8_t RoundKey[AES_keyExpSize];uint8_t Iv[AES_BLOCKLEN];};
void AES_init_ctx_key(struct AES_ctx* ctx, const uint8_t* key);
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv);
void AES_init_ctx_iv(struct AES_ctx*ctx,const uint8_t*key,const uint8_t*iv);
void AES_CBC_decrypt_buffer(struct AES_ctx*ctx,uint8_t*buf,size_t length);
#endif
