#pragma once

#include <stdbool.h>
#include <uv.h>

#include "common.h"
#include "server.h"

typedef struct _clientInfo clientInfo;

struct _clientInfo {
	uv_tcp_t *handle;
	HttpParserInfo *parserInfo;
	clientAnnounceData *announce;
};

clientInfo *newClient( void );
void freeClient( clientInfo *client );

int getClientIPFromSocket( clientInfo* client );
