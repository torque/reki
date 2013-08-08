#ifndef dbg_h
#define dbg_h

#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef NDEBUG
#define debug(M, ...)
#else
#define debug(M, ...) fprintf(stderr, "\033[1;34m>>\033[m %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#define log_err(M, ...) fprintf(stderr, "\033[1;31m>>\033[m (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define log_warn(M, ...) fprintf(stderr, "\033[1;33m>>\033[m (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define log_info(M, ...) fprintf(stderr, "\033[1;32m>>\033[m " M "\n", ##__VA_ARGS__)

#define check(A, M, ...) if(!(A)) { log_err(M, ##__VA_ARGS__); errno=0; goto error; }

#define sentinel(M, ...)  { log_err(M, ##__VA_ARGS__); errno=0; goto error; }

#define check_mem(A) check((A), "Memory allocation failed.")

#endif
