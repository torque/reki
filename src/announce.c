#include <ctype.h> // tolower
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <limits.h>

#include "announce.h"
#include "dbg.h"
#include "macros.h"

const char *AnnounceErrorMessage( AnnounceError error ) {
	const static char *AnnounceErrorStrings[] = {
		"There was no error.",
		"The request was malformed.",
		"The request was invalid (missing a field).",
		"The request contained a malformed field.",
		"The request contained a malformed peer_id.",
		"The request contained a malformed info_hash.",
		"The request contained a malformed ip.",
		"The request contained a malformed ipv4.",
		"The request contained a malformed ipv6.",
		"The request contained a malformed port.",
		"The requested torrent does not exist."
	};
	return AnnounceErrorStrings[error];
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

int decodeInfoHash( const char *input, size_t length, char **output ) {
	// kind of janky to hardcode the length. note: since snprintf null
	// terminates, output has to be an extra character in width to avoid
	// an OOB write.
	int o = 0;
	*output = malloc( 41*sizeof(**output) );
	for ( int i = 0; (i < length) && (o < 40); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return 1;
			(*output)[o++] = tolower( input[++i] );
			(*output)[o]   = tolower( input[++i] );
		} else
			snprintf( (*output) + o++, 3, "%02x", input[i] );
	}
	(*output)[o] = '\0';
	return 0;
}

ClientAnnounceData *ClientAnnounceData_new( void ) {
	ClientAnnounceData *announce = calloc( 1, sizeof(*announce) );
	announce->numwant = 20;
	announce->event   = AnnounceEvent_none;
	CompactAddress_init( announce->compact );
	return announce;
}

void ClientAnnounceData_free( ClientAnnounceData *announce ) {
	free( announce->infoHash );
	free( announce->id );
	free( announce );
}

enum _SeenFieldOffsets {
	SeenFieldOffset_peerID    = 1 << 0,
	SeenFieldOffset_infoHash  = 1 << 1,
	SeenFieldOffset_port      = 1 << 2,
	SeenFieldOffset_event     = 1 << 3,
	SeenFieldOffset_IP        = 1 << 4,
	SeenFieldOffset_IPv4      = 1 << 5,
	SeenFieldOffset_IPv6      = 1 << 6,
	SeenFieldOffset_left      = 1 << 7,
	SeenFieldOffset_required  = SeenFieldOffset_peerID | SeenFieldOffset_infoHash | SeenFieldOffset_port | SeenFieldOffset_left,
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
		// Compare keys to get values. This is not particularly elegant.
		if ( !(seenFields & SeenFieldOffset_peerID) && EqualLiteralLength( key, keyLength, "peer_id" ) ) {
			dbg_info( "peer_id: %.*s", (int)valueLength, value );
			CheckError( decodeURLString( value, valueLength, &announce->id ), errorCode = AnnounceError_malformedField );
			seenFields |= SeenFieldOffset_peerID;

		} else if ( !(seenFields & SeenFieldOffset_infoHash) && EqualLiteralLength( key, keyLength, "info_hash" ) ) {
			CheckError( decodeInfoHash( value, valueLength, &announce->infoHash ), errorCode = AnnounceError_malformedField );
			dbg_info( "info_hash: %.*s", 40, announce->infoHash );
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
			seenFields |= SeenFieldOffset_IPv4;

		} else if ( !(seenFields & SeenFieldOffset_IPv6) && EqualLiteralLength( key, keyLength, "ipv6" ) ) {
			dbg_info( "IPv6: %.*s", (int)valueLength, value);
			seenFields |= SeenFieldOffset_IPv6;

		} else if ( !(seenFields & SeenFieldOffset_port) && EqualLiteralLength( key, keyLength, "port" ) ) {
			dbg_info( "port: %.*s", (int)valueLength, value );
			announce->port = strtoul( value, NULL, 10 );
			seenFields |= SeenFieldOffset_port;

		} else if ( !(seenFields & SeenFieldOffset_left) && EqualLiteralLength( key, keyLength, "left" ) ) {
			dbg_info( "left: %.*s", (int)valueLength, value );
			announce->left = strtoull( value, NULL, 10 );
			seenFields |= SeenFieldOffset_left;

		}
	}
	// According to BEP23, not supporting non-compact responses is
	// allowed: http://bittorrent.org/beps/bep_0023.html

	CheckError( (seenFields & SeenFieldOffset_required) != SeenFieldOffset_required, errorCode = AnnounceError_missingField );

	return AnnounceError_okay;

error:
	announce->errorMessage = AnnounceErrorMessage( errorCode );
	return errorCode;
}
