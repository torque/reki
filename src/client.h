#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

typedef struct _ClientConnection ClientConnection;

#include "server.h"
#include "announce.h"


struct _ClientConnection {
	ServerHandle *handle;
	Server *server;
	HttpParserInfo *parserInfo;
	ClientAnnounceData *announce;
};

ClientConnection *ClientConnection_new( void );
void ClientConnection_free( ClientConnection *client );

int ClientConnection_getIPFromSocket( ClientConnection* client );
