#pragma once

#include <stdbool.h>
#include <uv.h>

// #include "../http_parser/http_parser.h"
#include "common.h"
#include "server.h"

typedef struct _clientInfo clientInfo;
// typedef struct _clientSettings clientSettings;

struct _clientInfo {
	uv_tcp_t *handle;
	httpParserInfo *parserInfo;
	clientAnnounceData *announce;
};

clientInfo *newClient( void );
void freeClient( clientInfo *client );

int getClientIp( clientInfo* client );
