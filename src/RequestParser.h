#pragma once

#include <stdbool.h>

typedef struct _HttpParserInfo HttpParserInfo;
typedef enum _HttpParserError HttpParserError;

#include "../http-parser/http_parser.h"
#include "StringBuffer.h"
#include "client.h"

enum _HttpParserError {
	ParserError_okay = 0,
	ParserError_httpParserError,
	ParserError_urlParserError,
};

struct _HttpParserInfo {
	http_parser *parser;
	http_parser_settings *settings;
	ClientConnection *client;

	// pointers into the client->readBuffer string.
	char *URLString;
	int URLStringLength;
	char *lastHeader;
	int lastHeaderLength;
	char *lastValue;
	int lastValueLength;

	struct http_parser_url *parsedURL;
	bool httpParserDone;
};

HttpParserInfo *HttpParser_new( void );
void HttpParser_free( HttpParserInfo *parserInfo );

HttpParserError HttpParser_parse( HttpParserInfo *parserInfo, const char *input, size_t length );
HttpParserError HttpParser_parseURL( HttpParserInfo *parserInfo, char **path, size_t *pathSize, char **query, size_t *querySize );
