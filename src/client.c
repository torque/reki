#include <stdlib.h>

#include "client.h"
#include "dbg.h"

ClientConnection *ClientConnection_new( void ) {
	ClientConnection *client = calloc( 1, sizeof(*client) );
	client->handle = calloc( 1, sizeof(*client->handle) );
	client->announce = calloc( 1, sizeof(*client->announce) );

	return client;
}

void ClientConnection_free( ClientConnection *client ) {
	if ( !client )
		return;

	free( client->announce );
	free( client->handle );
	free( client );
}

int ClientConnection_getIPFromSocket( ClientConnection* client ) {
	struct sockaddr_storage peerSocket;
	int ret, namelen = sizeof(peerSocket);
	ret = uv_tcp_getpeername( client->handle->tcpHandle, (struct sockaddr*)&peerSocket, &namelen );
	if ( ret ) {
		log_err( "Getpeername error: %s", uv_err_name( ret ) );
		return 1;
	}
	char ip[INET6_ADDRSTRLEN];
	int error = getnameinfo((struct sockaddr *)&peerSocket, peerSocket.ss_len, ip, sizeof(ip), NULL, 0, NI_NUMERICHOST);
	if ( error ) {
		log_err( "getnameinfo gone fucked up." );
		return 1;
	}
	client->announce->ipType = peerSocket.ss_family;
	client->announce->peerIp = ip;

	dbg_info( "Connection from: %s", ip );
	return 0;
}
