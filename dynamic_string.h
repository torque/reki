#ifndef dynamic_string_h
#define dynamic_string_h
#include <stdlib.h>
#include <strings.h>
#include <string.h>

typedef struct {
	char *str;
	size_t size;
	size_t alloc_size;
} dynamic_string;

dynamic_string *dynamic_string_init();
void dynamic_string_free(dynamic_string *str);
void dynamic_string_append(dynamic_string *str, const char *append, size_t size);
#endif
