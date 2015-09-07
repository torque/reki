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
#include "Scrape.h"

struct _ClientConnection {
	ServerHandle handle;
	Server *server;
	HttpParserInfo *parserInfo;
	StringBuffer *readBuffer;
	StringBuffer *writeBuffer;

	union _ClientRequestData {
		ClientAnnounceData *announce;
		ScrapeData *scrape;
	} request;

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
void Client_reply( ClientConnection *client );
#define Client_replyErrorLen( client, message ) Client_replyError( client, message, strlen(message) )
#define Client_CheckAllocReplyError( client, ptr ) if ( !ptr ) { Client_replyErrorLen( client, "An unknown error occurred." ); return; }
void Client_replyError( ClientConnection *client, const char *message, size_t messageLength );
void Client_terminate( ClientConnection *client );
