#include <stdlib.h>
#include <stdio.h> // snprintf
#include <ctype.h> // tolower
#include <limits.h> // uchar_max

#include "URLCommon.h"
#include "dbg.h"

int decodeURLString( const char *input, size_t length, char **output ) {
	// output is guaranteed to be the same size as the input or smaller.
	int o = 0;
	*output = malloc( (length + 1)*sizeof(**output) );
	for ( int i = 0; (i < length) && (o < length); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return -1;
			char encodedChar[3] = { input[i + 1], input[i + 2], '\0' };
			long decodedChar = strtol( encodedChar, NULL, 16 );
			if ( decodedChar < UCHAR_MAX )
				(*output)[o] = (char)decodedChar;
			else
				return -2;
			i += 2;
		} else
			(*output)[o] = input[i];
	}
	(*output)[o] = '\0';
	return o;
}

int decodeInfoHash( const char *input, size_t length, char *output ) {
	// kind of janky to hardcode the length. note: since snprintf null
	// terminates, output has to be an extra character in width to avoid
	// an OOB write.
	int o = 0;
	for ( int i = 0; (i < length) && (o < 40); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return -1;
			output[o++] = tolower( input[++i] );
			output[o]   = tolower( input[++i] );
		} else
			snprintf( output + o++, 3, "%02x", input[i] );
	}
	output[o] = '\0';
	return o;
}

int parseQueryString( const char *query, size_t length, QueryCallback *callback, void *callbackData ) {
	dbg_info( "Query: %.*s", (int)length, query );
	for ( int i = 0; i < length; i++ ) {
		char *key = (char *)(query + i), *value = key;
		for ( ; i < length; i++ ) {
			if ( query[i] == '=' ) {
				value = (char*)(query + i + 1);
			}
			else if ( query[i] == '&' ) {
				break;
			}
		}
		if ( value == key || value - query > length ) {
			dbg_info( "aaaa" );
			return 1;
		}

		size_t keyLength = value - key - 1, valueLength = query - value + i;
		dbg_info( "{ %.*s: %.*s }", (int)keyLength, key, (int)valueLength, value );

		int res = callback( callbackData, key, keyLength, value, valueLength );
		if ( res ) {
			dbg_info( "callback died" );
			return res;
		}
	}
	return 0;
}
