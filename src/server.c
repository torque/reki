#include <netinet/in.h> // struct sockaddr
#include <stdlib.h>  // calloc, malloc, free
#include <stdbool.h> // bool, true, false;

#include "server.h"
#include "macros.h"
#include "StringBuffer.h"
#include "client.h"
#include "RequestParser.h"
#include "../http-parser/http_parser.h"
#include "dbg.h"

// This buffer can be freed in the read callback.
static void uvReadBufferAlloc( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf ) {
	ClientConnection *client = handle->data;
	// append to the readbuffer.
	buf->base = client->readBuffer->str + client->readBuffer->size;
	// make sure the buffer has a length of at least one.
	buf->len = StringBuffer_ensureFreeSize( client->readBuffer, 1 );
}

static void closeClientConnection( uv_handle_t *handle ) {
	dbg_info( "final cleanup." );
	ClientConnection *client = handle->data;
	checktime( client, "Close connection." );
	free( handle );
	ClientConnection_free( client );
}

static void replyFinished( uv_write_t* reply, int status ) {
	dbg_info( "replyToClient" );
	if ( status < 0 ) {
		dbg_info( "Write error: %s", uv_err_name( status ) );
	}
	ClientConnection *client = reply->data;
	HttpParserInfo_free( client->parserInfo );
	free( reply );
	uv_close( (uv_handle_t*)client->handle->tcpHandle, closeClientConnection );
}

void replyWithError( ClientConnection *client, const char *message ) {

}

void replyToClient( ClientConnection *client, StringBuffer *message ) {
	dbg_info( "replyToClient" );
	uv_write_t *reply = calloc( 1, sizeof(*reply) );
	reply->data = (void*)client;

	uv_buf_t uvMessage = StringBuffer_toUvBuf( message );
	dbg_info( "buf_t: %.*s", (int)uvMessage.len, uvMessage.base );
	uv_write( reply, client->handle->stream, &uvMessage, 1, replyFinished );
}

#define processScrape( a, b ) dbg_info( "scraping" )

static int dispatchClient( ClientConnection *client ) {
	HttpParserInfo *parserInfo = client->parserInfo;

	http_parser_parse_url( parserInfo->URLString, parserInfo->URLStringLength, 0, parserInfo->parsedURL );

	const char *path  = parserInfo->URLString + parserInfo->parsedURL->field_data[UF_PATH].off;
	size_t pathSize   = parserInfo->parsedURL->field_data[UF_PATH].len;
	const char *query = parserInfo->URLString + parserInfo->parsedURL->field_data[UF_QUERY].off;
	size_t querySize  = parserInfo->parsedURL->field_data[UF_QUERY].len;
	dbg_info( "Requested path: %.*s", (int)pathSize, path );
	dbg_info( "Request query: %.*s", (int)querySize, query );
	if ( EqualLiteralLength( path, pathSize, "/announce" ) ) {
		ClientAnnounceData *announce = ClientAnnounceData_new( );
		if ( ClientAnnounceData_parseURLQuery( announce, query, querySize ) )
			log_warn( "%s", announce->errorMessage );
		else {
			dbg_info( "There was no error parsing the announce." );
			announce->score = uv_now( client->handle->stream->loop );
			client->request->announce = announce;
			client->requestType = ClientRequest_announce;
		}

	} else if ( EqualLiteralLength( path, pathSize, "/scrape" ) )
		processScrape( query, querySize );

	else
		log_warn( "Client made a bad request: %.*s", (int)parserInfo->URLStringLength, parserInfo->URLString );

	// This gets leaked, yo.
	StringBuffer *header = StringBuffer_initWithString( "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi", 0 );
	replyToClient( client, header );

	return 0;
}

static void readClientRequest( uv_stream_t *clientConnection, ssize_t nread, const uv_buf_t *buf ) {
	ClientConnection *client = clientConnection->data;
	if ( nread < 0 ) {
		dbg_info( "Read error %zd: %s", nread, uv_err_name( nread ) );
		// FREE PARSERINFO
		HttpParserInfo_free( client->parserInfo );
		// connection and handle are freed.
		uv_close( (uv_handle_t*)clientConnection, closeClientConnection );
		return;
	}

	size_t bytesParsed = http_parser_execute( client->parserInfo->parser, client->parserInfo->settings, buf->base, nread );
	dbg_info( "http_parser_execute has completed running." );
	// an error occurred, handle appropriately?
	if ( bytesParsed == 0 && (client->parserInfo->parser->http_errno || client->parserInfo->parser->upgrade) )
		log_warn( "http_parser_execute has failed." );

	if ( client->parserInfo->httpParserDone ) {
		uv_read_stop( client->handle->stream );
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
		ClientConnection *client = ClientConnection_new( );
#if defined(CLIENTTIMEINFO)
		client->startTime = uv_now( server->loop );
#endif
		client->handle->tcpHandle = clientConnection;
		clientConnection->data = (void*)client;

		int ret = ClientConnection_getIPFromSocket( client );
		if ( ret ) {
			log_err( "The peer's IP could not be divined." );
			// callback frees clientConnection and client
			uv_close( (uv_handle_t*)clientConnection, closeClientConnection );
			return;
		}

		HttpParserInfo *parserInfo = HttpParserInfo_new( );

		parserInfo->client = client;
		client->parserInfo = parserInfo;
		uv_read_start( client->handle->stream, uvReadBufferAlloc, readClientRequest );
	} else {
		uv_close( (uv_handle_t*)clientConnection, closeClientConnection );
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
		memcpy( outAddress, res->ai_addr, res->ai_addrlen );
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
			server->handle->tcpHandle = malloc( sizeof(*server->handle->tcpHandle) );
			checkFunction( uv_tcp_init( loop, server->handle->tcpHandle ) );
			break;
		}
		case ServerProtocol_UDP: {
			server->handle->udpHandle = malloc( sizeof(*server->handle->udpHandle) );
			checkFunction( uv_udp_init( loop, server->handle->udpHandle ) );
			break;
		}
		default: {
			log_err( "An unknown server protocol happened." );
			return 1;
		}
	}

	server->handle->stream->data = server;
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
			checkFunction( uv_tcp_bind( server->handle->tcpHandle, (struct sockaddr*)&address, flags ) );
			break;
		}
		case ServerProtocol_UDP: {
			checkFunction( uv_udp_bind( server->handle->udpHandle, (struct sockaddr*)&address, flags ) );
			log_err( "udp actually isn't supported yet." );
			return 1;
			// break;
		}
		default: {
			log_err( "An unknown server protocol happened." );
			return 1;
		}
	}

	// e = uv_udp_recv_start( server->handle->udpHandle, allocCB, readCB );
	checkFunction( uv_listen( server->handle->stream, 128, newClientConnection ) );

	char namebuf[INET6_ADDRSTRLEN];
	checkFunction( getnameinfo( (struct sockaddr *)&address, address.ss_len, namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST ) );
	dbg_info( "Listening on [%s]:%d.", namebuf, ntohs(((struct sockaddr_in*)&address)->sin_port) );

	return 0;
}
