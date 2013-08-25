#ifndef dbg_h
#define dbg_h

#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef NDEBUG
#define dbg_info(message, ...)
#define dbg_warn(message, ...)
#else
#define dbg_info(message, ...) fprintf(stderr, "\033[1;32m>>\033[m (%s:%d) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg_warn(message, ...) fprintf(stderr, "\033[1;33m>>\033[m (%s:%d) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#define log_info(message, ...) fprintf(stderr, "\033[1;32m>>\033[m " message "\n", ##__VA_ARGS__)
#define log_warn(message, ...) fprintf(stderr, "\033[1;33m>>\033[m (%s:%d) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define log_err(message, ...) fprintf(stderr, "\033[1;31m>>\033[m (%s:%d) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define fancy_perror(message) { fprintf(stderr, "\033[1;31m>>\033[m (%s:%d) ", __FILE__, __LINE__); perror(message); }
#define check(condition, message, ...) if(!(condition)) { log_warn(message, ##__VA_ARGS__); goto error; }
#define check_mem(mem) if (!mem) { log_err("\033[1;31mMemory allocation failed\033[m"); exit(1); }

#endif
