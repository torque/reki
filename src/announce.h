#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct _ClientAnnounceData ClientAnnounceData;
typedef enum   _AnnounceEvent AnnounceEvent;
typedef enum   _AnnounceError AnnounceError;

#include "CompactAddress.h"

enum _AnnounceError {
	AnnounceError_okay,
	AnnounceError_invalidRequest,
	AnnounceError_missingField,
	AnnounceError_malformedField,
	AnnounceError_malformedID,
	AnnounceError_malformedInfoHash,
	AnnounceError_malformedIP,
	AnnounceError_malformedIPv4,
	AnnounceError_malformedIPv6,
	AnnounceError_malformedPort,
	AnnounceError_noTorrent,
	AnnounceError_unknown,
};

struct _ClientAnnounceData {
	// The IPs and port info should probably be sanity-checked to avoid
	// sending junk info to clients. For later: check port within 0-65535
	// and ip within RFC 1918/4007 limits.

	char *id, *infoHash;
	char compact[CompactAddress_Size];
	// Will not serve more than 20 peers at a time anyway.
	uint8_t  numwant;
	// Currently unused.
	uint64_t uploaded, downloaded, left;
	// score to sort by in the ordered set.
	uint64_t score;
	// error handling.
	int seenFields;

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
AnnounceError ClientAnnounceData_fromQuery( ClientAnnounceData *announce, const char *query, size_t queryLength );
