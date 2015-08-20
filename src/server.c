#include <stdlib.h>
#include <stdbool.h>
#include <uv.h>

#include "server.h"
#include "StringBuffer.h"
#include "client.h"
#include "../http-parser/http_parser.h"
#include "dbg.h"

struct _httpParserInfo {
	http_parser *parser;
	http_parser_settings *settings;
	clientInfo *client;
	StringBuffer *urlBuffer;
	struct http_parser_url *url;
	bool lastHeaderFieldWasRealIP;
};

static void uvBufferAllocStaticCb( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf );
static int  httpUrlCb( http_parser *parser, const char *at, size_t length );
static int  httpHeaderFieldCb( http_parser *parser, const char *at, size_t length );
static int  httpHeaderValueCb( http_parser *parser, const char *at, size_t length );
static int  httpHeadersCompleteCb( http_parser *parser );
static void replyToClientCb( uv_write_t* reply, int status );
static int  getAddressInfo( const char *address, const char *port, struct sockaddr_storage *outAddress );

static httpParserInfo *newParserInfo( void ) {
	static http_parser_settings settings = {
		.on_url = httpUrlCb,
		.on_header_field = httpHeaderFieldCb,
		.on_header_value = httpHeaderValueCb,
		.on_headers_complete = httpHeadersCompleteCb
	};

	httpParserInfo *parserInfo = calloc( 1, sizeof(*parserInfo) );
	parserInfo->parser = calloc( 1, sizeof(*parserInfo->parser) );
	parserInfo->url = calloc( 1, sizeof(*parserInfo->url) );
	parserInfo->urlBuffer = StringBuffer_new( );
	parserInfo->settings = &settings;

	return parserInfo;
}

static void freeParserInfo( httpParserInfo *parserInfo ) {
	StringBuffer_free( parserInfo->urlBuffer );
	free( parserInfo->parser );
	free( parserInfo->url );
	free( parserInfo );
}

// Do this unless evidence occurs that it is a horrible idea.
static void uvBufferAllocStaticCb( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf ) {
	static char base[1024];
	buf->base = base;
	buf->len = sizeof(base);
}

static void replyToClientCb( uv_write_t* reply, int status ) {
	dbg_info( "replyToClient" );
	if ( status < 0 ) {
		dbg_info( "Write error: %s", uv_err_name( status ) );
	}
	clientInfo *client = reply->data;
	free( reply );
	freeParserInfo( client->parserInfo );
	freeClient( client );
}

static int httpUrlCb( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "httpUrl" );
	httpParserInfo *parserInfo = parser->data;
	StringBuffer_append( parserInfo->urlBuffer, at, length );
	return 0;
}

static int httpHeaderFieldCb( http_parser *parser, const char *at, size_t length ) {
	if ( length > 0 && strncmp( at, "X-Real-IP", length ) == 0 ) {
		dbg_info( "%.*s", (int)length, at );
		httpParserInfo *parserInfo = parser->data;
		parserInfo->lastHeaderFieldWasRealIP = true;
	}
	return 0;
}

static int httpHeaderValueCb( http_parser *parser, const char *at, size_t length ) {
	httpParserInfo *parserInfo = parser->data;
	if ( parserInfo->lastHeaderFieldWasRealIP ) {
		dbg_info( "%.*s", (int)length, at );
		parserInfo->lastHeaderFieldWasRealIP = false;

		// clientInfo *client = parserInfo->client;
		// account for null termination.
		// client->announce->peerIp = calloc( length+1, sizeof(*client->announce->peerIp) );
		// memcpy( client->announce->peerIp, at, length );
		// client->announce->peerIp = (char*)at;

		// struct sockaddr_storage clientSocket;
		// getAddressInfo( client->announce->peerIp, NULL, &clientSocket );
		// client->announce->ipType = clientSocket.ss_family;
	}
	return 0;
}

static int httpHeadersCompleteCb( http_parser *parser ) {
	dbg_info( "httpHeadersComplete" );
	httpParserInfo *parserInfo = parser->data;
	clientInfo *client = parserInfo->client;

	uv_read_stop( (uv_stream_t*)client->handle );

	uv_write_t *reply = calloc( 1, sizeof(*reply) );
	reply->data = (void*)client;

	http_parser_parse_url( parserInfo->urlBuffer->str, parserInfo->urlBuffer->size, 0, parserInfo->url );

	if ( parserInfo->url->field_set & (1 << UF_PATH) )
		dbg_info( "Path: %.*s", parserInfo->url->field_data[UF_PATH].len, parserInfo->urlBuffer->str + parserInfo->url->field_data[UF_PATH].off );

	if ( parserInfo->url->field_set & (1 << UF_QUERY) )
		dbg_info( "Query: %.*s", parserInfo->url->field_data[UF_QUERY].len, parserInfo->urlBuffer->str + parserInfo->url->field_data[UF_QUERY].off );

	uv_buf_t message = uv_buf_init( "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi", 85 );
	uv_write( reply, (uv_stream_t*)client->handle, &message, 1, replyToClientCb );
	return 0;
}

static void readClientRequest( uv_stream_t *clientConnection, ssize_t nread, const uv_buf_t *buf ) {
	clientInfo *client = clientConnection->data;
	if ( nread < 0 ) {
		dbg_info( "Read error %zd: %s", nread, uv_err_name( nread ) );
		uv_close( (uv_handle_t*)clientConnection, NULL );
		// FREE PARSERINFO AND CLIENTINFO
		freeParserInfo( client->parserInfo );
		freeClient( client );
		return;
	}

	size_t bytesParsed = http_parser_execute( client->parserInfo->parser, client->parserInfo->settings, buf->base, nread );
	dbg_info( "http_parser_execute has completed running." );
	// an error occurred, handle appropriately?
	if ( bytesParsed == 0 && (client->parserInfo->parser->http_errno || client->parserInfo->parser->upgrade) )
		log_warn( "http_parser_execute has failed." );
}

static void newClientConnection( uv_stream_t *server, int status ) {
	if ( status < 0 ) {
		log_warn( "Connection error: %s", uv_err_name( status ) );
		return;
	}
	dbg_info( "Connection received." );

	uv_tcp_t *clientConnection = calloc( 1, sizeof *clientConnection );
	uv_tcp_init( server->loop, clientConnection );

	if ( uv_accept( server, (uv_stream_t*)clientConnection ) == 0 ) {
		clientInfo *client = newClient( );
		client->handle = clientConnection;

		int ret = getClientIp( client );
		if ( ret ) {
			log_err( "The peer's IP could not be divined." );
			uv_close( (uv_handle_t*)clientConnection, NULL );
			freeClient( client );
			return;
		}

		httpParserInfo *parserInfo = newParserInfo( );
		// check NULL
		http_parser_init( parserInfo->parser, HTTP_REQUEST );
		// add references to our data structures
		parserInfo->parser->data = (void*)parserInfo;
		parserInfo->client = client;
		client->parserInfo = parserInfo;
		clientConnection->data = (void*)client;
		uv_read_start( (uv_stream_t*)clientConnection, uvBufferAllocStaticCb, readClientRequest );
	} else {
		uv_close( (uv_handle_t*)clientConnection, NULL );
	}
}

static int getAddressInfo( const char *address, const char *port, struct sockaddr_storage *outAddress ) {
	struct addrinfo hints, *res;
	memset( &hints, 0, sizeof(hints) );
	// OS X manpage says PF_UNSPEC but linux says AF_UNSPEC. PF_UNSPEC is
	// defined as AF_UNSPEC on OS X, so we'll just use that. They're both
	// defined as 0, so this isn't even technically necessary.
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST;
	int e = getaddrinfo( address, port, &hints, &res );
	if (e) {
		log_err("getaddrinfo failed: %s", gai_strerror(e));
		return 1;
	} else {
		if ( res && res->ai_addrlen )
			memcpy( outAddress, res->ai_addr, res->ai_addrlen );
		else
			return 1;
		freeaddrinfo( res );
	}
	return 0;
}

int createServer( uv_loop_t *loop, uv_tcp_t *server ) {
	int e = uv_tcp_init( loop, server );
	if ( e ) {
		log_err( "uv_tcp_init failed: %s", uv_err_name( e ) );
		return 1;
	}

	struct sockaddr_storage address;
	e = getAddressInfo( "::", "9001", &address );
	if ( e ) {
		log_err( "getAddressInfo failed." );
		return 1;
	}

	e = uv_tcp_bind( server, (struct sockaddr*)&address, 0 );
	if ( e ) {
		log_err( "uv_tcp_bind failed: %s", uv_err_name( e ) );
		return 1;
	}

	e = uv_listen( (uv_stream_t*)server, 128, newClientConnection );
	if ( e ) {
		log_err( "uv_listen error: %s", uv_err_name( e ) );
		return 1;
	}

	dbg_info( "Listening on port %d.", ntohs(((struct sockaddr_in*)&address)->sin_port) );

	return 0;
}
