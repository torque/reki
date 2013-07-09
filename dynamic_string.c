#include "dynamic_string.h"

#ifndef DYNAMICSTRINGSIZE
#define DYNAMICSTRINGSIZE 1024
#endif

dynamic_string *dynamic_string_init() {
	dynamic_string *str = (dynamic_string *)malloc(sizeof(dynamic_string));
	str->alloc_size = DYNAMICSTRINGSIZE;
	str->size = 0;
	str->str = (char*)malloc(sizeof(char)*str->alloc_size);
	return str;
}

void dynamic_string_free(dynamic_string *str) {
	free(str->str);
	free(str);
}

void dynamic_string_append(dynamic_string *str, const char *append, size_t size) {
	while(str->size + size > str->alloc_size) {
		str->str = (char *)realloc((void *)str->str, str->alloc_size*2);
		str->alloc_size *= 2;
	}

	memcpy(((void*)str->str) + str->size, (void *)append, size);
	str->size += size;
}
