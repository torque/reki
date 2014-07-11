#pragma once

typedef struct {
	char *str;
	size_t size;
	size_t alloc_size;
} dynamic_string;

dynamic_string* dynamic_string_init( void );
void dynamic_string_free( dynamic_string *str );
void dynamic_string_append( dynamic_string *str, const char *append, size_t size );
void dynamic_string_join( dynamic_string *joinee, dynamic_string *joiner );
