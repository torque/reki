#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "dbg.h"

ClientConnection *ClientConnection_new( void ) {
	ClientConnection *client = malloc( sizeof(*client) );
	client->handle = malloc( sizeof(*client->handle) );
	client->readBuffer = StringBuffer_new( );
	client->writeBuffer = StringBuffer_new( );
	memset( client->compactAddress, 0, AddressOffset_Size );
	memcpy( client->compactAddress + AddressOffset_IPv4Bencode,  "6:", 2 );
	memcpy( client->compactAddress + AddressOffset_IPv6Bencode, "18:", 3 );

	return client;
}

void ClientConnection_free( ClientConnection *client ) {
	if ( !client )
		return;

	ClientAnnounceData_free( client->announce );
	StringBuffer_free( client->readBuffer  );
	StringBuffer_free( client->writeBuffer );
	free( client->handle );
	free( client );
}

int ClientConnection_getIPFromString( ClientConnection *client, const char *address, const char *port ) {
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
		goto error;
	else {
		switch ( res->ai_family ) {
			case AF_INET: {
				memcpy( client->compactAddress + AddressOffset_IPv4Address, &((struct sockaddr_in*)res->ai_addr)->sin_addr, 4 );
				if ( port )
					memcpy( client->compactAddress + AddressOffset_IPv4Port , &((struct sockaddr_in*)res->ai_addr)->sin_port, 2 );
				break;
			}

			case AF_INET6: {
				memcpy( client->compactAddress + AddressOffset_IPv6Address, &((struct sockaddr_in6*)res->ai_addr)->sin6_addr, 16 );
				if ( port )
					memcpy( client->compactAddress + AddressOffset_IPv6Port , &((struct sockaddr_in6*)res->ai_addr)->sin6_port,  2 );
				break;
			}

			default:
				goto freeaddress;
		}
	}

	freeaddrinfo( res );
	return 0;

freeaddress:
	log_err( "don't recognize address family!!!" );
	freeaddrinfo( res );

error:
	log_err( "getaddrinfo failed: %s", gai_strerror(e) );
	return 1;
}


int ClientConnection_getIPFromSocket( ClientConnection* client ) {
	struct sockaddr_storage peerSocket;
	int ret, namelen = sizeof(peerSocket);
	ret = uv_tcp_getpeername( client->handle->tcpHandle, (struct sockaddr*)&peerSocket, &namelen );
	if ( ret ) {
		log_err( "Getpeername error: %s", uv_err_name( ret ) );
		return 1;
	}

	switch ( peerSocket.ss_family ) {
		case AF_INET: {
			memcpy( client->compactAddress + AddressOffset_IPv4Address, &((struct sockaddr_in*)&peerSocket)->sin_addr, 4 );
			break;
		}
		case AF_INET6: {
			memcpy( client->compactAddress + AddressOffset_IPv6Address, &((struct sockaddr_in6*)&peerSocket)->sin6_addr, 16 );
			break;
		}
		default: {
			dbg_info( "getIPFromSocket unknown family????" );
			return 1;
		}
	}

	return 0;
}
