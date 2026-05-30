#pragma once

#ifndef SIPHON_PLATFORM_H
#define SIPHON_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint8_t* out, size_t len);

#endif