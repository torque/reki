#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <limits.h>

#include "announce.h"
#include "dbg.h"
#include "macros.h"

const static char *AnnounceErrorStrings[] = {
	"There was no error.",
	"The request was malformed.",
	"The request was invalid (missing a field).",
	"The request was invalid (a field was malformed).",
	"The requested torrent does not exist."
};

ClientAnnounceData *ClientAnnounceData_new( void ) {
	ClientAnnounceData *announce = calloc( 1, sizeof(*announce) );
	announce->numwant = 50;
	announce->compact = true;
	announce->event   = AnnounceEvent_none;
	return announce;
}

int decodeURLString( const char *input, size_t length, char **output ) {
	// output is guaranteed to be the same size as the input or smaller.
	int o = 0;
	*output = malloc( (length + 1)*sizeof(**output) );
	for ( int i = 0; (i < length) && (o < length); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return 1;
			char encodedChar[3] = { input[i + 1], input[i + 2], '\0' };
			long decodedChar = strtol( encodedChar, NULL, 16 );
			if ( decodedChar < UCHAR_MAX )
				(*output)[o] = (char)decodedChar;
			else
				return 1;
			i += 2;
		} else
			(*output)[o] = input[i];
	}
	(*output)[o] = '\0';
	return 0;
}

void ClientAnnounceData_free( ClientAnnounceData *announce ) {
	free( announce->infoHash );
	free( announce->id );
	free( announce->ip );
	free( announce );
}

enum _SeenFieldOffsets {
	SeenFieldOffset_peerID    = 1 << 0,
	SeenFieldOffset_infoHash  = 1 << 1,
	SeenFieldOffset_port      = 1 << 2,
	SeenFieldOffset_event     = 1 << 3,
	SeenFieldOffset_IP        = 1 << 4,
	SeenFieldOffset_compact   = 1 << 5,
	SeenFieldOffset_IPv4      = 1 << 6,
	SeenFieldOffset_IPv6      = 1 << 7,
	SeenFieldOffset_required  = SeenFieldOffset_peerID | SeenFieldOffset_infoHash | SeenFieldOffset_port,
	SeenFieldOffset_important = SeenFieldOffset_required | SeenFieldOffset_IP,
};

#define CheckError( boolean, setError ) if ( boolean ) { setError; goto error; }

AnnounceError ClientAnnounceData_parseURLQuery( ClientAnnounceData *announce, const char *query, size_t querySize ) {
	int seenFields = 0;
	AnnounceError errorCode = AnnounceError_okay;
	dbg_info( "Query: %.*s", (int)querySize, query );
	for ( int i = 0; i < querySize; i++ ) {
		char *key = (char *)(query + i), *value = key;
		for ( ; i < querySize; i++ ) {
			if ( query[i] == '=' ) {
				value = (char*)(query + i + 1);
			}
			else if ( query[i] == '&' ) {
				break;
			}
		}
		CheckError( value == key || value - query > querySize, errorCode = AnnounceError_invalidRequest );

		ptrdiff_t keyLength = value - key - 1, valueLength = query - value + i;
		dbg_info( "key: %.*s; value: %.*s", (int)keyLength, key, (int)valueLength, value );
		// compare keys to get values.
		if ( !(seenFields & SeenFieldOffset_peerID) && EqualLiteralLength( key, keyLength, "peer_id" ) ) {
			dbg_info( "peer_id: %.*s", (int)valueLength, value );
			CheckError( decodeURLString( value, valueLength, &announce->id ), errorCode = AnnounceError_malformedField );
			seenFields |= SeenFieldOffset_peerID;
		} else if ( !(seenFields & SeenFieldOffset_infoHash) && EqualLiteralLength( key, keyLength, "info_hash" ) ) {
			CheckError( decodeURLString( value, valueLength, &announce->infoHash ), errorCode = AnnounceError_malformedField );
			dbg_info( "info_hash: %.*s", 20, announce->infoHash );
			seenFields |= SeenFieldOffset_infoHash;
		// The IP value in the request can allegedly be a DNS name,
		// according to BEP3[1]. I don't know if any clients do this, but
		// it's not currently supported.
		// [1]: http://bittorrent.org/beps/bep_0003.html#trackers
		} else if ( !(seenFields & SeenFieldOffset_IP) && EqualLiteralLength( key, keyLength, "ip" ) ) {
			dbg_info( "IP: %.*s", (int)valueLength, value );

			seenFields |= SeenFieldOffset_IP;

		// Optional fields according to BEP7[1], I don't know if clients
		// tend to send these or not. Not supported for now.
		// [1]: http://bittorrent.org/beps/bep_0007.html#announce-parameter
		} else if ( !(seenFields & SeenFieldOffset_IPv4) && EqualLiteralLength( key, keyLength, "ipv4" ) ) {
			dbg_info( "IPv4: %.*s", (int)valueLength, value);

		} else if ( !(seenFields & SeenFieldOffset_IPv6) && EqualLiteralLength( key, keyLength, "ipv6" ) ) {
			dbg_info( "IPv6: %.*s", (int)valueLength, value);

		} else if ( !(seenFields & SeenFieldOffset_port) && EqualLiteralLength( key, keyLength, "port" ) ) {
			dbg_info( "port: %.*s", (int)valueLength, value );
			announce->port = strtoul( value, NULL, 10 );
			seenFields |= SeenFieldOffset_port;

		} else if ( !(seenFields & SeenFieldOffset_compact) && EqualLiteralLength( key, keyLength, "compact" ) ) {
			dbg_info( "compact: %.*s", (int)valueLength, value );
			if ( value[0] == '0' )
				announce->compact = false;
			seenFields |= SeenFieldOffset_compact;

		}

		// This shortcut will have to go if statistics ever get added and we
		// want all fields.
		if ( (seenFields & SeenFieldOffset_important) == SeenFieldOffset_important )
			goto announce;
	}

	CheckError( (seenFields & SeenFieldOffset_required) != SeenFieldOffset_required, errorCode = AnnounceError_missingField );

announce:
	return AnnounceError_okay;

error:
	announce->errorMessage = AnnounceErrorStrings[errorCode];
	return errorCode;
}
