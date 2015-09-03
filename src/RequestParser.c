#include <stdlib.h>
#include <string.h>

#include "../http-parser/http_parser.h"

#include "RequestParser.h"
#include "macros.h"
#include "dbg.h"

struct _HttpParserInfo {
	http_parser *parser;
	http_parser_settings *settings;

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

static int httpURL( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "httpUrl" );
	HttpParserInfo *parserInfo = parser->data;
	if ( !parserInfo->URLString )
		parserInfo->URLString = (char*)at;
	parserInfo->URLStringLength += length;

	return 0;
}

// both of these can be called multiple times in case of chunked input.
static int httpHeaderField( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "Header: %.*s", (int)length, at );
	HttpParserInfo *parserInfo = parser->data;
	// lastValue has been set, which means we've already seen the x-real-
	// IP header, if it was sent.
	if ( parserInfo->lastValue ) {
		if ( parserInfo->httpParserDone ) return 0;

		parserInfo->httpParserDone = true;
		return 0;
	}

	if ( !parserInfo->lastHeader )
		parserInfo->lastHeader = (char*)at;
	parserInfo->lastHeaderLength += length;

	return 0;
}

// value also follows a header cb.
static int httpHeaderValue( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "httpHeaderValueCb" );
	HttpParserInfo *parserInfo = parser->data;
	// x-real ip has already been seen, and the parsing work is done.
	if ( parserInfo->httpParserDone ) return 0;

	if ( EqualLiteralLength( parserInfo->lastHeader, parserInfo->lastHeaderLength, "X-Real-IP" ) ) {
		if ( !parserInfo->lastValue )
			parserInfo->lastValue = (char*)at;
		parserInfo->lastValueLength += length;
	}

	return 0;
}

static int httpHeadersComplete( http_parser *parser ) {
	dbg_info( "httpHeadersComplete" );
	HttpParserInfo *parserInfo = parser->data;
	if ( !parserInfo->httpParserDone ) {
		parserInfo->httpParserDone = true;
		// maybe return an error to abort parsing at this point?
	}

	return 0;
}

HttpParserInfo *HttpParser_new( void ) {
	static http_parser_settings settings = {
		.on_url              = httpURL,
		.on_header_field     = httpHeaderField,
		.on_header_value     = httpHeaderValue,
		.on_headers_complete = httpHeadersComplete
	};

	HttpParserInfo *parserInfo = malloc( sizeof(*parserInfo) );
	parserInfo->parsedURL = malloc( sizeof(*parserInfo->parsedURL) );
	parserInfo->parser = malloc( sizeof(*parserInfo->parser) );
	// check all these allocs maybe???

	http_parser_init( parserInfo->parser, HTTP_REQUEST );

	// callback state
	parserInfo->URLString = NULL;
	parserInfo->URLStringLength = 0;
	parserInfo->lastHeader = NULL;
	parserInfo->lastHeaderLength = 0;
	parserInfo->lastValue = NULL;
	parserInfo->lastValueLength = 0;

	parserInfo->httpParserDone = false;

	parserInfo->settings = &settings;
	parserInfo->parser->data = parserInfo;

	return parserInfo;
}

void HttpParser_free( HttpParserInfo *parserInfo ) {
	if ( !parserInfo ) return;

	dbg_info("httpParser_free");
	free( parserInfo->parser );
	free( parserInfo->parsedURL );
	free( parserInfo );
}

bool HttpParser_done( HttpParserInfo *parserInfo ) {
	return parserInfo->httpParserDone;
}

HttpParserError HttpParser_parse( HttpParserInfo *parserInfo, const char *input, size_t length ) {
	http_parser_execute( parserInfo->parser, parserInfo->settings, input, length );
	dbg_info( "http_parser_execute has completed running." );

	if ( parserInfo->parser->http_errno || parserInfo->parser->upgrade )
		return ParserError_httpParserError;

	return ParserError_okay;
}

HttpParserError HttpParser_parseURL( HttpParserInfo *parserInfo, char **path, size_t *pathSize, char **query, size_t *querySize ) {
	if ( http_parser_parse_url( parserInfo->URLString, parserInfo->URLStringLength, 0, parserInfo->parsedURL ) )
		return ParserError_urlParserError;

	*path      = parserInfo->URLString + parserInfo->parsedURL->field_data[UF_PATH].off;
	*pathSize  = parserInfo->parsedURL->field_data[UF_PATH].len;
	*query     = parserInfo->URLString + parserInfo->parsedURL->field_data[UF_QUERY].off;
	*querySize = parserInfo->parsedURL->field_data[UF_QUERY].len;

	return ParserError_okay;
}

// char address[parserInfo->lastValueLength+1];
// memcpy( address, parserInfo->lastValue, parserInfo->lastValueLength );
// address[parserInfo->lastValueLength] = '\0';
// ClientConnection_IPFromString( parserInfo->client, address, NULL );

// char address[parserInfo->lastValueLength+1];
// memcpy( address, parserInfo->lastValue, parserInfo->lastValueLength );
// address[parserInfo->lastValueLength] = '\0';
// ClientConnection_IPFromString( parserInfo->client, address, NULL );
