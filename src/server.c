#include <netinet/in.h> // struct sockaddr
#include <stdlib.h>  // calloc, malloc, free
#include <stdbool.h> // bool, true, false;

#include "server.h"
#include "macros.h"
#include "StringBuffer.h"
#include "client.h"
#include "../http-parser/http_parser.h"
#include "dbg.h"

struct _HttpParserInfo {
	http_parser *parser;
	http_parser_settings *settings;
	clientInfo *client;
	StringBuffer *urlBuffer;
	struct http_parser_url *url;
	bool lastHeaderFieldWasRealIP;
	bool httpParserDone;
};

static void uvBufferAllocStaticCb( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf );
static int  httpUrlCb( http_parser *parser, const char *at, size_t length );
static int  httpHeaderFieldCb( http_parser *parser, const char *at, size_t length );
static int  httpHeaderValueCb( http_parser *parser, const char *at, size_t length );
static int  httpHeadersCompleteCb( http_parser *parser );
static void replyToClientCb( uv_write_t* reply, int status );
static int  getAddressInfo( const char *address, const char *port, struct sockaddr_storage *outAddress );

static HttpParserInfo *newParserInfo( void ) {
	static http_parser_settings settings = {
		.on_url = httpUrlCb,
		.on_header_field = httpHeaderFieldCb,
		.on_header_value = httpHeaderValueCb,
		.on_headers_complete = httpHeadersCompleteCb
	};

	HttpParserInfo *parserInfo = calloc( 1, sizeof(*parserInfo) );
	parserInfo->parser = calloc( 1, sizeof(*parserInfo->parser) );
	parserInfo->url = calloc( 1, sizeof(*parserInfo->url) );
	parserInfo->urlBuffer = StringBuffer_new( );
	parserInfo->settings = &settings;

	return parserInfo;
}

static void freeParserInfo( HttpParserInfo *parserInfo ) {
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
	freeParserInfo( client->parserInfo );
	freeClient( client );
	free( reply );
}

static int httpUrlCb( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "httpUrl" );
	HttpParserInfo *parserInfo = parser->data;
	StringBuffer_append( parserInfo->urlBuffer, at, length );
	return 0;
}

static int httpHeaderFieldCb( http_parser *parser, const char *at, size_t length ) {
	if ( length > 0 && strncmp( at, "X-Real-IP", length ) == 0 ) {
		dbg_info( "%.*s", (int)length, at );
		HttpParserInfo *parserInfo = parser->data;
		parserInfo->lastHeaderFieldWasRealIP = true;
	}
	return 0;
}

static int httpHeaderValueCb( http_parser *parser, const char *at, size_t length ) {
	dbg_info( "httpHeaderValueCb" );
	HttpParserInfo *parserInfo = parser->data;
	if ( parserInfo->lastHeaderFieldWasRealIP ) {
		dbg_info( "%.*s", (int)length, at );
		parserInfo->lastHeaderFieldWasRealIP = false;
		parserInfo->httpParserDone = true;

		clientInfo *client = parserInfo->client;
		// account for null termination.
		client->announce->peerIp = calloc( length+1, sizeof(*client->announce->peerIp) );
		memcpy( client->announce->peerIp, at, length );
		client->announce->peerIp = (char*)at;

		struct sockaddr_storage clientSocket;
		getAddressInfo( client->announce->peerIp, NULL, &clientSocket );
		client->announce->ipType = clientSocket.ss_family;
	}
	return 0;
}

static int httpHeadersCompleteCb( http_parser *parser ) {
	dbg_info( "httpHeadersComplete" );
	HttpParserInfo *parserInfo = parser->data;
	if ( !parserInfo->httpParserDone )
		parserInfo->httpParserDone = true;

	return 0;
}

void replyToClient( clientInfo *client, StringBuffer *message ) {
	dbg_info( "replyToClient" );
	uv_write_t *reply = calloc( 1, sizeof(*reply) );
	reply->data = (void*)client;

	uv_buf_t uvMessage = StringBuffer_toUvBuf( message );
	dbg_info( "buf_t: %.*s", (int)uvMessage.len, uvMessage.base );
	uv_write( reply, (uv_stream_t*)client->handle, &uvMessage, 1, replyToClientCb );
}

#define processAnnounce( a ) dbg_info( "announcing" )
#define processScrape( a ) dbg_info( "scraping" )

static int dispatchClient( clientInfo *client ) {
	HttpParserInfo *parserInfo = client->parserInfo;

	http_parser_parse_url( parserInfo->urlBuffer->str, parserInfo->urlBuffer->size, 0, parserInfo->url );

	const char *path = parserInfo->urlBuffer->str + parserInfo->url->field_data[UF_PATH].off;
	size_t pathSize  = parserInfo->url->field_data[UF_PATH].len;
	dbg_info( "Requested path: %.*s", (int)pathSize, path );
	if ( EqualLiteral( path, "/announce" ) )
		processAnnounce( client );
	else if ( EqualLiteral( path, "/scrape" ) )
		processScrape( client );
	else {
		log_warn( "Client made a bad request: %.*s", (int)parserInfo->urlBuffer->size, parserInfo->urlBuffer->str );
	}

	// This gets leaked, yo.
	StringBuffer *header = StringBuffer_initWithString( "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi", 0 );
	replyToClient( client, header );

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

	if ( client->parserInfo->httpParserDone ) {
		uv_read_stop( (uv_stream_t*)client->handle );
		dispatchClient( client );
	}
}

static void newClientConnection( uv_stream_t *server, int status ) {
	if ( status < 0 ) {
		log_warn( "Connection error: %s", uv_err_name( status ) );
		return;
	}
	dbg_info( "Connection received." );

	uv_tcp_t *clientConnection = calloc( 1, sizeof(*clientConnection) );
	uv_tcp_init( server->loop, clientConnection );

	if ( uv_accept( server, (uv_stream_t*)clientConnection ) == 0 ) {
		clientInfo *client = newClient( );
		client->handle = clientConnection;

		int ret = getClientIPFromSocket( client );
		if ( ret ) {
			log_err( "The peer's IP could not be divined." );
			uv_close( (uv_handle_t*)clientConnection, NULL );
			freeClient( client );
			return;
		}

		HttpParserInfo *parserInfo = newParserInfo( );
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

Server *Server_new( const char *bindIP, const char *port, ServerProtocol type ) {
	Server *newServer = calloc( 1, sizeof(*newServer) );
	newServer->bindIP = strdup( bindIP );
	newServer->bindPort = strdup( port );
	newServer->protocol = type;
	newServer->handle = calloc( 1, sizeof(*newServer->handle) );

	return newServer;
}

int Server_initWithLoop( Server *server, uv_loop_t *loop ) {
	switch ( server->protocol ) {
		case ServerProtocol_TCP: {
			server->handle->tcpServer = malloc( sizeof(*server->handle->tcpServer) );
			checkFunction( uv_tcp_init( loop, server->handle->tcpServer ) );
			break;
		}
		case ServerProtocol_UDP: {
			server->handle->udpServer = malloc( sizeof(*server->handle->udpServer) );
			checkFunction( uv_udp_init( loop, server->handle->udpServer ) );
			break;
		}
		default: {
			log_err( "An unknown server protocol happened." );
			return 1;
		}
	}
	return 0;
}

int Server_listen( Server *server ) {
	struct sockaddr_storage address;
	checkFunction( getAddressInfo( server->bindIP, server->bindPort, &address ) );
	server->ipFamily = address.ss_family;

	unsigned int flags = 0;
	if ( address.ss_family == AF_INET6 )
		flags |= IPV6_V6ONLY;

	switch ( server->protocol ) {
		case ServerProtocol_TCP: {
			checkFunction( uv_tcp_bind( server->handle->tcpServer, (struct sockaddr*)&address, flags ) );
			break;
		}
		case ServerProtocol_UDP: {
			checkFunction( uv_udp_bind( server->handle->udpServer, (struct sockaddr*)&address, flags ) );
			log_err( "udp actually isn't supported yet." );
			return 1;
			// break;
		}
		default: {
			log_err( "An unknown server protocol happened." );
			return 1;
		}
	}

	// e = uv_udp_recv_start( server->handle->udpServer, allocCB, readCB );
	checkFunction( uv_listen( server->handle->stream, 128, newClientConnection ) );

	char namebuf[INET6_ADDRSTRLEN];
	checkFunction( getnameinfo( (struct sockaddr *)&address, address.ss_len, namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST ) );
	dbg_info( "Listening on [%s]:%d.", namebuf, ntohs(((struct sockaddr_in*)&address)->sin_port) );

	return 0;
}
