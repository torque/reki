#include <stdlib.h>

#include "client.h"
#include "dbg.h"

int getClientIPFromSocket( clientInfo* client ) {
	struct sockaddr_storage peerSocket;
	int ret, namelen = sizeof(peerSocket);
	ret = uv_tcp_getpeername( client->handle, (struct sockaddr*)&peerSocket, &namelen );
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

clientInfo *newClient( void ) {
	clientInfo *client = calloc( 1, sizeof(*client) );
	client->announce = calloc( 1, sizeof(*client->announce) );

	return client;
}

void freeClient( clientInfo *client ) {
	free( client->announce );
	free( client );
}
