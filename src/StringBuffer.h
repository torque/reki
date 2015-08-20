#pragma once
#include <stddef.h> // size_t
#include <uv.h>

typedef struct _StringBuffer StringBuffer;

struct _StringBuffer {
	char *str;
	size_t size;
	size_t alloc_size;
};

StringBuffer *StringBuffer_new( void );
StringBuffer *StringBuffer_initWithString( const char *src, size_t size );
void StringBuffer_free( StringBuffer *str );
void StringBuffer_append( StringBuffer *str, const char *append, size_t size );
void StringBuffer_joinBufs( StringBuffer *joinee, StringBuffer *joiner );
uv_buf_t StringBuffer_toUvBuf( StringBuffer *src );
