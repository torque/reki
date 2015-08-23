#pragma once

#include <stdbool.h>
#include <uv.h>

#include "common.h"
#include "server.h"

typedef struct _ClientConnection ClientConnection;

struct _ClientConnection {
	ServerHandle *handle;
	Server *server;
	HttpParserInfo *parserInfo;
	clientAnnounceData *announce;
};

ClientConnection *ClientConnection_new( void );
void ClientConnection_free( ClientConnection *client );

int ClientConnection_getIPFromSocket( ClientConnection* client );
