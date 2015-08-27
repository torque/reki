#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "StringBuffer.h"
#include "dbg.h"

#ifndef DEFAULT_BUFFER_SIZE
#define DEFAULT_BUFFER_SIZE 1024
#endif

StringBuffer* StringBuffer_new( void ) {
	StringBuffer *buffer = malloc( sizeof(*buffer) );
	buffer->alloc_size = DEFAULT_BUFFER_SIZE;
	buffer->str = malloc( buffer->alloc_size * sizeof(*buffer->str) );
	buffer->size = 0;
	return buffer;
}

StringBuffer *StringBuffer_initWithString( const char *src, size_t size ) {
	StringBuffer *newBuffer = StringBuffer_new( );
	StringBuffer_append( newBuffer, src, size? size: strlen( src ) );
	return newBuffer;
}

void StringBuffer_free( StringBuffer *buf ) {
	if ( !buf ) return;

	free( buf->str );
	free( buf );
}

static void StringBuffer_grow( StringBuffer *buf, size_t size ) {
	while ( buf->alloc_size < size ) {
		buf->alloc_size *= 1.5f;
		buf->str = realloc( buf->str, sizeof(*buf->str) * buf->alloc_size );
	}
}

void StringBuffer_append( StringBuffer *buf, const char *append, size_t size ) {
	StringBuffer_grow( buf, buf->size + size );

	memcpy( buf->str + buf->size, append, size );
	buf->size += size;
}

size_t StringBuffer_ensureFreeSize( StringBuffer *buf, size_t size ) {
	StringBuffer_grow( buf, buf->size + size );

	return buf->alloc_size - buf->size;
}

// This is not too good because it will truncate the format string, but
// I want something that doesn't need to allocate a temp buffer for the
// format string.
void StringBuffer_sprintf( StringBuffer *buf, const char *format, ... ) {
	va_list args;
  va_start( args, format );
	int added = vsnprintf( buf->str + buf->size, buf->alloc_size - buf->size, format, args );
	va_end( args );
	buf->size += added;
}

// TURNS OUT VASPRINTF AINT POSIX AND I DONT WANT TO USE DUMB WEIRD
// DEFINES ON DIFFERENT PLATFORMS SO
void StringBuffer_safeSprintf( StringBuffer *buf, const char *format, ... ) {
	va_list args, args2;
	va_start( args, format );
	va_copy( args2, args );

	int length = vsnprintf( NULL, 0, format, args );
	va_end( args );

	StringBuffer_grow( buf, length );
	int added = vsnprintf( buf->str + buf->size, length, format, args2 );
	buf->size += added;
	va_end( args2 );
}

void StringBuffer_join( StringBuffer *joinee, StringBuffer *joiner ) {
	StringBuffer_append( joinee, joiner->str, joiner->size );
}

uv_buf_t StringBuffer_toUvBuf( StringBuffer *buf ) {
	return uv_buf_init( buf->str, buf->size );
}
