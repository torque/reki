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
