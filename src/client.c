#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "dbg.h"

ClientConnection *Client_new( void ) {
	ClientConnection *client = malloc( sizeof(*client) );
	client->handle = malloc( sizeof(*client->handle) );
	client->request = malloc( sizeof(*client->request) );
	client->readBuffer = StringBuffer_new( );
	client->writeBuffer = StringBuffer_new( );
	memset( client->compactAddress, 0, AddressOffset_Size );
	memcpy( client->compactAddress + AddressOffset_IPv4Bencode,  "6:", 2 );
	memcpy( client->compactAddress + AddressOffset_IPv6Bencode, "18:", 3 );

	return client;
}

void Client_free( ClientConnection *client ) {
	if ( !client )
		return;

	switch ( client->requestType ) {
		case ClientRequest_announce: {
			ClientAnnounceData_free( client->request->announce );
			break;
		}

		case ClientRequest_scrape:
			break;
	}
	StringBuffer_free( client->readBuffer  );
	StringBuffer_free( client->writeBuffer );
	free( client->request );
	free( client->handle );
	free( client );
}

static void Client_cleanup( uv_handle_t *clientHandle ) {
	dbg_info( "final cleanup." );
	ClientConnection *client = handle->data;
	checktime( client, "Close connection." );
	HttpParserInfo_free( client->parserInfo );
	ClientConnection_free( client );
	// this is the client tcp handle.
	free( handle );
}

void Client_terminate( ClientConnection *client ) {
	uv_close( (uv_handle_t*)client->handle->tcpHandle, Client_cleanup );
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

static void Client_reply( ClientConnection *client ) {
	uv_write_t *reply = malloc( sizeof(*reply) );
	reply->data = client;

	uv_buf_t uvMessage = StringBuffer_toUvBuf( client->writeBuffer );
	dbg_info( "Client_reply message: %.*s", (int)uvMessage.len, uvMessage.base );
	uv_write( reply, client->handle->stream, &uvMessage, 1, Client_replyDone );
}

#define processScrape( a, b ) dbg_info( "scraping" )

static void Client_route( ClientConnection *client ) {
	HttpParserInfo *parserInfo = client->parserInfo;

	const char *path, *query;
	size_t pathSize, querySize;
	HttpParser_parseURL( parserInfo, &path, &pathSize, &query, &querySize );

	dbg_info( "Requested path: %.*s", (int)pathSize, path );
	dbg_info( "Request query: %.*s", (int)querySize, query );
	if ( EqualLiteralLength( path, pathSize, "/announce" ) ) {
		ClientAnnounceData *announce = ClientAnnounceData_new( );
		if ( ClientAnnounceData_parseURLQuery( announce, query, querySize ) )
			log_warn( "%s", announce->errorMessage );
		else {
			dbg_info( "There was no error parsing the announce." );
			// announce has no access to the loop.
			announce->score = uv_now( client->handle->stream->loop );
			client->request->announce = announce;
			client->requestType = ClientRequest_announce;
		}

	} else if ( EqualLiteralLength( path, pathSize, "/scrape" ) )
		processScrape( query, querySize );

	else {
		log_warn( "Client made a bad request: %.*s", (int)parserInfo->URLStringLength, parserInfo->URLString );
		const static char *invalidRoute = "HTTP/1.0 403 Forbidden\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length:0\r\n\r\n";
		StringBuffer_append( client->writeBuffer, invalidRoute, strlen( invalidRoute ) );
		Client_reply( client );
		return;
	}

	const static char *okayRoute = "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length:";
	// "%d\r\n\r\n"
	StringBuffer_append( client->writeBuffer, okayRoute, strlen( okayRoute ) );
	MemoryStore_processAnnounce( client->server->memStore, client );
}

static void Client_readRequest( uv_stream_t *clientConnection, ssize_t nread, const uv_buf_t *buf ) {
	ClientConnection *client = clientConnection->data;
	if ( nread < 0 ) {
		dbg_info( "Read error %zd: %s", nread, uv_err_name( nread ) );
		// ParserInfo isn't freed by the client termination, but perhaps it
		// should be. I don't know if an error here means the stream is in a
		// writable state or not, but I assume not.

		Client_terminate( client );
		return;
	}

	if ( nread > 0 ) {
		if ( HttpParserInfo_parse( client->parserInfo, buf->base, nread ) ) {
			// Should actually reply to the peer in this case?
			Client_terminate( client );
			return;
		}
	}

	if ( client->parserInfo->httpParserDone ) {
		uv_read_stop( client->handle->stream );
		Client_route( client );
	}
}

void Client_handleConnection( ClientConnection *client ) {
	client->parserInfo = HttpParserInfo_new( );
	client->parserInfo->client = client;

	uv_read_start( client->handle->stream, Client_allocReadBuffer, Client_readRequest );
}

ClientError Client_IPFromString( ClientConnection *client, const char *address, const char *port ) {
	ClientError status;
	struct addrinfo hints, *res;
	memset( &hints, 0, sizeof(hints) );
	// OS X manpage says PF_UNSPEC but linux says AF_UNSPEC. PF_UNSPEC is
	// defined as AF_UNSPEC on OS X, so we'll just use that. They're both
	// defined as 0.
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST;
	// uv_getaddrinfo is async. If hostname lookup is ever implemented,
	// this will probably need to be switched.
	int e = getaddrinfo( address, port, &hints, &res );
	if ( e )
		goto getaddrinfoFailed;

	switch ( res->ai_family ) {
		case AF_INET: {
			memcpy( client->compactAddress + AddressOffset_IPv4Address, &((struct sockaddr_in*)res->ai_addr)->sin_addr, 4 );
			if ( port )
				memcpy( client->compactAddress + AddressOffset_IPv4Port , &((struct sockaddr_in*)res->ai_addr)->sin_port, 2 );
			client->compactAddress[0] |= CompactAddress_IPv4Flag;
			break;
		}

		case AF_INET6: {
			memcpy( client->compactAddress + AddressOffset_IPv6Address, &((struct sockaddr_in6*)res->ai_addr)->sin6_addr, 16 );
			if ( port )
				memcpy( client->compactAddress + AddressOffset_IPv6Port , &((struct sockaddr_in6*)res->ai_addr)->sin6_port,  2 );
			client->compactAddress[0] |= CompactAddress_IPv6Flag;
			break;
		}

		default:
			goto UnknownAddress;
	}

	freeaddrinfo( res );
	return ClientError_okay;

UnknownAddress:
	log_err( "IPFromString: unknown address family: %d", res->ai_family );
	freeaddrinfo( res );
	status = ClientError_unknownAddressFamily;
	goto error;

getaddrinfoFailed:
	log_err( "getaddrinfo failed: %s", gai_strerror( e ) );
	status = ClientError_getaddrinfoFailed;

error:
	return status;
}


ClientError Client_IPFromSocket( ClientConnection* client ) {
	ClientError status;
	struct sockaddr_storage peerSocket;
	int namelen = sizeof(peerSocket);
	int e = uv_tcp_getpeername( client->handle->tcpHandle, (struct sockaddr*)&peerSocket, &namelen );
	if ( e )
		goto getpeernameFailed;

	switch ( peerSocket.ss_family ) {
		case AF_INET: {
			memcpy( client->compactAddress + AddressOffset_IPv4Address, &((struct sockaddr_in*)&peerSocket)->sin_addr, 4 );
			client->compactAddress[0] |= CompactAddress_IPv4Flag;
			break;
		}
		case AF_INET6: {
			memcpy( client->compactAddress + AddressOffset_IPv6Address, &((struct sockaddr_in6*)&peerSocket)->sin6_addr, 16 );
			client->compactAddress[0] |= CompactAddress_IPv6Flag;
			break;
		}
		default:
			goto UnknownAddress;
	}

	return ClientError_okay;

UnknownAddress:
	dbg_info( "IPFromSocket: unknown address family: %d", peerSocket.ss_family );
	status = ClientError_unknownAddressFamily;

getpeernameFailed:
	log_err( "getpeername failed: %s", uv_err_name( e ) );
	status = ClientError_getpeernameFailed;

error:
	return status;
}
