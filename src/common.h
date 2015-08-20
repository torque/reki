#pragma once

#include <stdbool.h>

typedef struct _clientAnnounceData clientAnnounceData;

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
