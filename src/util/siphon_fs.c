#include "siphon_fs.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <stdlib.h>

static wchar_t* widen(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    UINT cp = CP_UTF8;
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
        if (n <= 0) return NULL;
    }
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(cp, 0, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

FILE* siphon_fopen(const char* path, const char* mode) {
    wchar_t* wpath = widen(path);
    wchar_t* wmode = widen(mode);
    FILE* f = NULL;
    if (wpath && wmode) f = _wfopen(wpath, wmode);
    free(wpath);
    free(wmode);
    return f;
}

int siphon_mkdir(const char* path) {
    wchar_t* wpath = widen(path);
    int ret;
    if (!wpath) return -1;
    ret = _wmkdir(wpath);
    free(wpath);
    return ret;
}

#else
#include <sys/stat.h>

FILE* siphon_fopen(const char* path, const char* mode) {
    return fopen(path, mode);
}

int siphon_mkdir(const char* path) {
    return mkdir(path, 0755);
}
#endif
