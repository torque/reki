#include <stdlib.h>
#include <string.h>
#include "dynamic_string.h"

#ifndef DYNAMICSTRINGSIZE
#define DYNAMICSTRINGSIZE 1024
#endif

dynamic_string* dynamic_string_init( void ) {
	dynamic_string *str = calloc( 1, sizeof *str );
	str->alloc_size = DYNAMICSTRINGSIZE;
	str->size = 0;
	str->str = calloc( str->alloc_size, sizeof str->str );
	return str;
}

void dynamic_string_free( dynamic_string *str ) {
	free( str->str );
	free( str );
}

void dynamic_string_append( dynamic_string *str, const char *append, size_t size ) {
	while(str->size + size > str->alloc_size) {
		str->str = realloc( str->str, str->alloc_size*2 );
		str->alloc_size *= 2;
	}

	memcpy( str->str + str->size, append, size );
	str->size += size;
}

void dynamic_string_join( dynamic_string *joinee, dynamic_string *joiner ) {
	dynamic_string_append( joinee, joiner->str, joiner->size );
	dynamic_string_free( joiner );
}
