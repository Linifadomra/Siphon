#include "siphon_log.h"
#include <stdio.h>
#include <stdarg.h>

static siphon_log_fn g_fn = NULL;
static void*         g_ud = NULL;

void siphon_log_set(siphon_log_fn fn, void* userdata) {
    g_fn = fn;
    g_ud = userdata;
}

void siphon_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n < sizeof(buf) && buf[n-1] == '\n') buf[n-1] = '\0';
    if (g_fn) {
        g_fn(g_ud, buf);
    } else {
        fputs(buf, stderr);
        fputc('\n', stderr);
    }
}
