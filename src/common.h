#pragma once

#include <uv.h> // libuv
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>
#include <stdbool.h>

#include "dynamic_string.h"
#include "../http-parser/http_parser.h"

typedef struct _clientSettings clientSettings;
typedef struct _clientInfo clientInfo;
typedef struct _clientAnnounceData clientAnnounceData;

struct _clientSettings {
	http_parser parser;
	http_parser_settings parserSettings;
};

struct _clientInfo {
	uv_tcp_t *handle;
	dynamic_string *reqUrl;
	struct http_parser_url parsedUrl;
	bool lastHeaderFieldWasRealIP;
	clientSettings *settings;
	clientAnnounceData *announce;
};

struct _clientAnnounceData {
	char peerId[20];
	char infoHash[40];
	uint8_t peerIdLen;
	uint8_t infoHashLen;
	char *peerIp;
	int ipType;
	int port;
	unsigned long long uploaded;
	unsigned long long downloaded;
	unsigned long long left;
	bool compact;
	enum {
		STARTED,
		COMPLETED,
		STOPPED,
		UNKNOWN
	} event;
	int numwant;
};
