#include <stdlib.h>
#include <string.h>

#include "RequestParser.h"
#include "macros.h"
#include "dbg.h"

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

		char address[parserInfo->lastValueLength+1];
		memcpy( address, parserInfo->lastValue, parserInfo->lastValueLength );
		address[parserInfo->lastValueLength] = '\0';
		ClientConnection_getIPFromString( parserInfo->client, address, NULL );

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
		// if X-Real-IP is the last header, it needs to be parsed here.
		if ( parserInfo->lastValue ) {
			char address[parserInfo->lastValueLength+1];
			memcpy( address, parserInfo->lastValue, parserInfo->lastValueLength );
			address[parserInfo->lastValueLength] = '\0';
			ClientConnection_getIPFromString( parserInfo->client, address, NULL );
		}
		parserInfo->httpParserDone = true;
		// maybe return an error to abort parsing at this point?
	}

	return 0;
}

HttpParserInfo *HttpParserInfo_new( void ) {
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

void HttpParserInfo_free( HttpParserInfo *parserInfo ) {
	if ( !parserInfo ) return;

	dbg_info("httpParserInfo_free");
	free( parserInfo->parser );
	free( parserInfo->parsedURL );
	free( parserInfo );
}
