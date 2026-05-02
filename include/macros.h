#ifndef SIPHON_MACROS_H
#define SIPHON_MACROS_H

#include <sys/stat.h>

#if defined(_WIN32)
  #include <direct.h>
  #define MKDIR_ONE(p) _mkdir(p)
  #define strdup _strdup
  #define IS_REG_FILE(st) ((st).st_mode & _S_IFREG)
#else
  #include <unistd.h>
  #define MKDIR_ONE(p) mkdir((p), 0755)
  #define IS_REG_FILE(st) S_ISREG((st).st_mode)
#endif

#endif
