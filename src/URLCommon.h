#pragma once

#include <stddef.h> // size_t

// I don't like typedefs to hide pointers.
typedef int (QueryCallback)( void *data, const char *key, size_t keyLength, const char *value, size_t valueLength );

int decodeURLString( const char *input, size_t length, char *output, size_t outputLength );
int decodeInfoHash( const char *input, size_t length, char *output, size_t outputLength );
int parseQueryString( const char *query, size_t length, QueryCallback *callback, void *callbackData );
