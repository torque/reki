#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct _ClientAnnounceData ClientAnnounceData;
typedef enum   _AnnounceEvent AnnounceEvent;
typedef enum   _AnnounceError AnnounceError;

#include "../http-parser/http_parser.h"

enum _AnnounceError {
	AnnounceError_okay,
	AnnounceError_invalidRequest,
	AnnounceError_missingField,
	AnnounceError_malformedField,
	AnnounceError_noTorrent,
};

struct _ClientAnnounceData {
	// The IPs and port info should probably be sanity-checked to avoid
	// sending junk info to clients. For later: check port within 0-65535
	// and ip within RFC 1918/4007 limits.

	// Of these, port is the only one that need not be copied, since it
	// won't contain urlencoded characters that need to be decoded.
	char *id, *infoHash, *ip;
	int IPType;
	unsigned long port;
	// Will not serve more than 50 peers at a time anyway.
	uint8_t  numwant;
	// Currently unused.
	uint64_t uploaded, downloaded, left;

	bool compact;
	enum _AnnounceEvent {
		AnnounceEvent_none,
		AnnounceEvent_start,
		AnnounceEvent_complete,
		AnnounceEvent_stop,
		AnnounceEvent_unknown,
	} event;

	const char *errorMessage;
};

const char *AnnounceErrorMessage( AnnounceError error );
ClientAnnounceData *ClientAnnounceData_new( void );
void ClientAnnounceData_free( ClientAnnounceData *announce );
AnnounceError ClientAnnounceData_parseURLQuery( ClientAnnounceData *announce, const char *query, size_t querySize );
