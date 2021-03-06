#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "dbg.h"
#include "macros.h"

ClientConnection *Client_new( void ) {
	ClientConnection *client = malloc( sizeof(*client) );
	if ( !client ) goto badClient;

	client->readBuffer = StringBuffer_new( );
	if ( !client->readBuffer ) goto badReadBuffer;

	client->writeBuffer = StringBuffer_new( );
	if ( !client->writeBuffer ) goto badWriteBuffer;

	client->request.announce = NULL;

	return client;

badWriteBuffer:
	StringBuffer_free( client->readBuffer );
badReadBuffer:
	free( client );
badClient:
	return NULL;
}

void Client_free( ClientConnection *client ) {
	if ( !client ) return;
	dbg_info( "Client_free" );

	switch ( client->requestType ) {
		case ClientRequest_announce: {
			ClientAnnounceData_free( client->request.announce );
			break;
		}

		case ClientRequest_scrape: {
			ScrapeData_free( client->request.scrape );
			break;
		}
	}
	StringBuffer_free( client->readBuffer  );
	StringBuffer_free( client->writeBuffer );
	free( client );
}

static void Client_cleanup( uv_handle_t *handle ) {
	ClientConnection *client = handle->data;
	checktime( client, "Close connection." );
	HttpParser_free( client->parserInfo );
	Client_free( client );
	// this is the client tcp handle.
	free( handle );
}

void Client_terminate( ClientConnection *client ) {
	uv_close( (uv_handle_t*)client->handle.tcpHandle, Client_cleanup );
}

static void Client_allocReadBuffer( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf ) {
	ClientConnection *client = handle->data;
	buf->base = client->readBuffer->str + client->readBuffer->size;
	// make sure the buffer has a length of at least one.
	buf->len = StringBuffer_ensureFreeSize( client->readBuffer, 1 );
}

static void Client_replyDone( uv_write_t* reply, int status ) {
	// it doesn't matter if there was an error replying, because either
	// way we're closing the connection.
	Client_terminate( reply->data );
	free( reply );
}

void Client_reply( ClientConnection *client ) {
	uv_write_t *reply = malloc( sizeof(*reply) );
	if ( !reply ) {
		Client_terminate( client );
		return;
	}

	reply->data = client;

	uv_buf_t uvMessage = StringBuffer_toUvBuf( client->writeBuffer );
	dbg_info( "Client_reply message: %.*s", (int)uvMessage.len, uvMessage.base );
	uv_write( reply, client->handle.stream, &uvMessage, 1, Client_replyDone );
}

#define ErrorFormat "d14:failure reason%lu:%se"
void Client_replyError( ClientConnection *client, const char *message, size_t messageLength ) {
	// I wonder if snprintf is optimized to not eat up a whole bunch of
	// time in the case that n is 0
	int length = snprintf( NULL, 0, ErrorFormat, messageLength, message );
	StringBuffer_sprintf( client->writeBuffer, "%u\r\n\r\n" ErrorFormat, length, messageLength, message );
	Client_reply( client );
}
#undef ErrorFormat

static void Client_route( ClientConnection *client ) {
	#define OkayRoute "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length:"
	#define InvalidRoute "HTTP/1.0 403 Forbidden\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length:12\r\n\r\nGET WRECKED\n"

	HttpParserInfo *parserInfo = client->parserInfo;

	char *path, *query;
	size_t pathSize, querySize;
	HttpParser_parseURL( parserInfo, &path, &pathSize, &query, &querySize );

	dbg_info( "Requested path: %.*s", (int)pathSize, path );
	dbg_info( "Request query: %.*s", (int)querySize, query );
	if ( EqualLiteralLength( path, pathSize, "/announce" ) ) {
		StringBuffer_append( client->writeBuffer, OkayRoute, strlen( OkayRoute ) );
		ClientAnnounceData *announce = ClientAnnounceData_new( );
		Client_CheckAllocReplyError( client, announce );

		announce->score = uv_now( client->handle.stream->loop );
		client->requestType = ClientRequest_announce;
		client->request.announce = announce;
		if ( ClientAnnounceData_fromQuery( announce, query, querySize ) ) {
			log_warn( "%s", announce->errorMessage );
			Client_replyErrorLen( client, announce->errorMessage );
			return;
		}

		dbg_info( "There was no error parsing the announce." );
		if ( announce->event == AnnounceEvent_stop ) {
			StringBuffer_append( client->writeBuffer, "6\r\n\r\n3:bye\n", 11 );
			Client_reply( client );
			return;
		}

		// check if ip has been set
		if ( !(announce->compact[0] & CompactAddress_IPv4Flag) && !(announce->compact[0] & CompactAddress_IPv6Flag) ) {
			// read from socket and header.
			char *xRealIP = HttpParser_realIP( client->parserInfo );
			if ( xRealIP ) {
				CompactAddress_fromString( announce->compact, xRealIP, NULL );
				free( xRealIP );
			} else {
				struct sockaddr_storage sock;
				int len = sizeof(sock);
				int e = uv_tcp_getpeername( client->handle.tcpHandle, (struct sockaddr*)&sock, &len );
				if ( e ) {
					Client_replyErrorLen( client, "IP could not be determined." );
					return;
				}

				CompactAddress_fromSocket( announce->compact, &sock, false );
			}
		}
		CompactAddress_dump( announce->compact );
		MemoryStore_processAnnounce( client->server->memStore, client );

	} else if ( EqualLiteralLength( path, pathSize, "/scrape" ) ) {
		StringBuffer_append( client->writeBuffer, OkayRoute, strlen( OkayRoute ) );
		ScrapeData *scrape = ScrapeData_new( );
		Client_CheckAllocReplyError( client, scrape );

		client->requestType = ClientRequest_scrape;
		client->request.scrape = scrape;
		if ( ScrapeData_fromQuery( scrape, query, querySize ) ) {
			Client_replyErrorLen( client, "Invalid scrape request." );
			return;
		}

		MemoryStore_processScrape( client->server->memStore, client );

	} else {
		StringBuffer_append( client->writeBuffer, InvalidRoute, strlen( InvalidRoute ) );
		Client_reply( client );
	}
	#undef OkayRoute
	#undef InvalidRoute
}

static void Client_readRequest( uv_stream_t *clientConnection, ssize_t nread, const uv_buf_t *buf ) {
	ClientConnection *client = clientConnection->data;
	if ( nread < 0 ) {
		dbg_info( "Read error %zd: %s", nread, uv_err_name( nread ) );

		Client_terminate( client );
		return;
	}

	if ( nread > 0 ) {
		if ( HttpParser_parse( client->parserInfo, buf->base, nread ) ) {
			// Should actually reply to the peer in this case?
			Client_terminate( client );
			return;
		}
	}

	if ( HttpParser_done( client->parserInfo ) ) {
		uv_read_stop( client->handle.stream );
		Client_route( client );
	}
}

void Client_handleConnection( ClientConnection *client ) {
	client->parserInfo = HttpParser_new( );
	if ( !client->parserInfo ) {
		Client_replyErrorLen( client, "An unknown error occurred." );
		return;
	}

	uv_read_start( client->handle.stream, Client_allocReadBuffer, Client_readRequest );
}
