#include <stdlib.h>
#include <string.h>

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

void StringBuffer_free( StringBuffer *str ) {
	free( str->str );
	free( str );
}

void StringBuffer_append( StringBuffer *str, const char *append, size_t size ) {
	while ( str->size + size > str->alloc_size ) {
		str->alloc_size *= 1.5f;
		str->str = realloc( str->str, str->alloc_size );
	}

	memcpy( str->str + str->size, append, size );
	str->size += size;
}

void StringBuffer_join( StringBuffer *joinee, StringBuffer *joiner ) {
	StringBuffer_append( joinee, joiner->str, joiner->size );
	StringBuffer_free( joiner );
}

uv_buf_t StringBuffer_toUvBuf( StringBuffer *src ) {
	return uv_buf_init( src->str, src->size );
}
