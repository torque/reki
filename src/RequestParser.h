#pragma once

#include <stdbool.h>

typedef struct _HttpParserInfo HttpParserInfo;
typedef enum _HttpParserError HttpParserError;

enum _HttpParserError {
	ParserError_okay = 0,
	ParserError_httpParserError,
	ParserError_urlParserError,
};

HttpParserInfo *HttpParser_new( void );
void HttpParser_free( HttpParserInfo *parserInfo );

HttpParserError HttpParser_parse( HttpParserInfo *parserInfo, const char *input, size_t length );
HttpParserError HttpParser_parseURL( HttpParserInfo *parserInfo, char **path, size_t *pathSize, char **query, size_t *querySize );
