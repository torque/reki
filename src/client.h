#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

typedef struct _ClientConnection ClientConnection;
typedef union  _ClientRequestData ClientRequestData;
typedef enum   _ClientRequestType ClientRequestType;

#include "server.h"
#include "StringBuffer.h"
#include "RequestParser.h"
#include "announce.h"

typedef struct {
} ClientScrapeData;

struct _ClientConnection {
	ServerHandle *handle;
	Server *server;
	HttpParserInfo *parserInfo;
	StringBuffer *readBuffer;
	StringBuffer *writeBuffer;

	union _ClientRequestData {
		ClientAnnounceData *announce;
		ClientScrapeData *scrape;
	} *request;

	enum _ClientRequestType {
		ClientRequest_announce,
		ClientRequest_scrape,
	} requestType;

#if defined(CLIENTTIMEINFO)
	uint64_t startTime;
#endif
};

ClientConnection *Client_new( void );
void Client_free( ClientConnection *client );
void Client_handleConnection( ClientConnection *client );
void Client_terminate( ClientConnection *client );
