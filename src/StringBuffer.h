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
void StringBuffer_free( StringBuffer *buf );
void StringBuffer_append( StringBuffer *buf, const char *append, size_t size );
void StringBuffer_join( StringBuffer *joinee, StringBuffer *joiner );
size_t StringBuffer_ensureFreeSize( StringBuffer *buf, size_t size );
void StringBuffer_sprintf( StringBuffer *buf, const char *format, ... );
void StringBuffer_safeSprintf( StringBuffer *buf, const char *format, ... );
uv_buf_t StringBuffer_toUvBuf( StringBuffer *buf );
