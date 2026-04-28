#ifndef SIPHON_LOG_H
#define SIPHON_LOG_H

typedef void (*siphon_log_fn)(void* userdata, const char* msg);

void siphon_log_set(siphon_log_fn fn, void* userdata);
void siphon_log(const char* fmt, ...);

#endif
