#pragma once

#include <stdbool.h>

typedef struct _HttpParserInfo HttpParserInfo;

#include "../http-parser/http_parser.h"
#include "StringBuffer.h"
#include "client.h"

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

HttpParserInfo *HttpParserInfo_new( void );
void HttpParserInfo_free( HttpParserInfo *parserInfo );
