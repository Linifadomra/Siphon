#ifndef SIPHON_FS_H
#define SIPHON_FS_H

#include <stdio.h>

FILE* siphon_fopen(const char* path, const char* mode);
int siphon_mkdir(const char* path);

#endif
