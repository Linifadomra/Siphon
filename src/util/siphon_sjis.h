#ifndef SIPHON_SJIS_H
#define SIPHON_SJIS_H

#include <stddef.h>

size_t siphon_sjis_to_utf8(const char* in, char* out, size_t outSize);

#endif
