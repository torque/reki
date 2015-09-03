#include <netinet/in.h> // struct sockaddr
#include <stdlib.h>  // calloc, malloc, free
#include <stdbool.h> // bool, true, false;
#include <stdint.h>

#include "server.h"
#include "macros.h"
#include "StringBuffer.h"
#include "client.h"
#include "dbg.h"

static void Server_newTCPConnection( uv_stream_t *server, int status ) {
	if ( status < 0 ) {
		log_warn( "Connection error: %s", uv_err_name( status ) );
		return;
	}
	dbg_info( "Connection received." );

	ClientConnection *client = Client_new( );
	client->handle->tcpHandle = malloc( sizeof(*client->handle->tcpHandle) );
	uv_tcp_init( server->loop, client->handle->tcpHandle );
	client->handle->tcpHandle->data = client;

	if ( uv_accept( server, client->handle->stream ) == 0 ) {
#if defined(CLIENTTIMEINFO)
		client->startTime = uv_now( server->loop );
#endif
		Client_handleConnection( client );
	} else {
		Client_terminate( client );
	}
}

static int Server_AddressInfo( Server *server, struct sockaddr_storage *outAddress ) {
	struct addrinfo hints, *res;
	memset( &hints, 0, sizeof(hints) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST;

	int e = getaddrinfo( server->bindIP, server->bindPort, &hints, &res );
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
	checkFunction( Server_AddressInfo( server, &address ) );
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
			log_err( "UDP actually isn't supported yet." );
			return 1;
			// break;
		}
		default: {
			log_err( "An unknown server protocol happened." );
			return 1;
		}
	}

	// e = uv_udp_recv_start( server->handle->udpHandle, allocCB, readCB );
	checkFunction( uv_listen( server->handle->stream, 128, Server_newTCPConnection ) );

	char namebuf[INET6_ADDRSTRLEN];
	checkFunction( getnameinfo( (struct sockaddr *)&address, sizeof(address), namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST ) );
	dbg_info( "Listening on [%s]:%d.", namebuf, ntohs(((struct sockaddr_in*)&address)->sin_port) );

	return 0;
}
