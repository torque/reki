#include <ctype.h> // tolower
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
	"The request contained a malformed field.",
	"The request contained a malformed peer_id.",
	"The request contained a malformed info_hash.",
	"The request contained a malformed ip.",
	"The request contained a malformed ipv4.",
	"The request contained a malformed ipv6.",
	"The request contained a malformed port.",
	"The requested torrent does not exist."
};

static int decodeURLString( const char *input, size_t length, char **output ) {
	// output is guaranteed to be the same size as the input or smaller.
	int o = 0;
	*output = malloc( (length + 1)*sizeof(**output) );
	for ( int i = 0; (i < length) && (o < length); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return -1;
			char encodedChar[3] = { input[i + 1], input[i + 2], '\0' };
			long decodedChar = strtol( encodedChar, NULL, 16 );
			if ( decodedChar < UCHAR_MAX )
				(*output)[o] = (char)decodedChar;
			else
				return -2;
			i += 2;
		} else
			(*output)[o] = input[i];
	}
	(*output)[o] = '\0';
	return o;
}

static int decodeInfoHash( const char *input, size_t length, char **output ) {
	// kind of janky to hardcode the length. note: since snprintf null
	// terminates, output has to be an extra character in width to avoid
	// an OOB write.
	int o = 0;
	*output = malloc( 41*sizeof(**output) );
	for ( int i = 0; (i < length) && (o < 40); i++, o++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return -1;
			(*output)[o++] = tolower( input[++i] );
			(*output)[o]   = tolower( input[++i] );
		} else
			snprintf( (*output) + o++, 3, "%02x", input[i] );
	}
	(*output)[o] = '\0';
	return o;
}

static int handleIPv4( ClientAnnounceData *announce, const char *value, size_t valueLength ) {
	char *ipv4;
	int decodedLength = decodeURLString( value, valueLength, &ipv4 );
	// 0.0.0.0 the shortest ipv4 address?
	if ( decodedLength < 7 ) return 1;

	char *port;
	int portLength = 0;
	for ( int i = 7; i < decodedLength; i++ ) {
		if ( ipv4[i] == ':' ) {
			ipv4[i] = '\0';
			port = ipv4 + i + 1;
			portLength = decodedLength - i - 1;
			dbg_info( "handleIPv4: %s : %s; %d", ipv4, port, portLength );
		}
	}

	int status = CompactAddress_fromString( announce->compact, ipv4, portLength? port: NULL );
	if ( portLength )
		announce->compact[0] |= CompactAddress_IPv4PortFlag;

	free( ipv4 );
	return status;
}

static int handleIPv6( ClientAnnounceData *announce, const char *value, size_t valueLength ) {
	// ipv6 can be of form: [::1]:9001 or ::1
	char *ipv6;
	int decodedLength = decodeURLString( value, valueLength, &ipv6 );
	if ( decodedLength < 2 ) return 1;

	// have to use a scratch pointer because incrementing ipv6 means
	// freeing is invalid, and keeping track of whether or not it has been
	// incremented is probably worse than just using a scratch pointer.
	char *port, *scratch = ipv6;
	int portLength = 0;
	if ( scratch[0] == '[' ) {
		// chop off the [
		scratch++;
		for( int i = 2; i < decodedLength - 1; i++ ) {
			if ( scratch[i] == ']' ) {
				// null terminate ipv6. port will already be null terminated
				// because decodeURLString null terminates its output.
				scratch[i] = '\0';
				port = scratch + i + 2;
				portLength = decodedLength - i - 3;
				dbg_info( "handleIPv6: %s : %s; %d", scratch, port, portLength );
				if ( portLength < 1 || portLength > 5 || i < 2 ) return 1;
				break;
			}
		}
	}

	int status = CompactAddress_fromString( announce->compact, scratch, portLength? port: NULL );
	if ( portLength )
		announce->compact[0] |= CompactAddress_IPv6PortFlag;

	free( ipv6 );
	return status;
}

ClientAnnounceData *ClientAnnounceData_new( void ) {
	ClientAnnounceData *announce = calloc( 1, sizeof(*announce) );
	announce->numwant = 20;
	announce->event   = AnnounceEvent_none;
	CompactAddress_init( announce->compact );
	return announce;
}

void ClientAnnounceData_free( ClientAnnounceData *announce ) {
	dbg_info( "ClientAnnounceData_free" );
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
			CheckError( decodeURLString( value, valueLength, &announce->id ) < 1, errorCode = AnnounceError_malformedID );
			seenFields |= SeenFieldOffset_peerID;

		} else if ( !(seenFields & SeenFieldOffset_infoHash) && EqualLiteralLength( key, keyLength, "info_hash" ) ) {
			CheckError( decodeInfoHash( value, valueLength, &announce->infoHash ) < 1, errorCode = AnnounceError_malformedInfoHash );
			dbg_info( "info_hash: %.*s", 40, announce->infoHash );
			seenFields |= SeenFieldOffset_infoHash;

		// The IP value in the request can allegedly be a DNS name,
		// according to BEP3[1]. I don't know if any clients do this, but
		// it's not currently supported.
		// [1]: http://bittorrent.org/beps/bep_0003.html#trackers
		} else if ( !(seenFields & SeenFieldOffset_IP) && EqualLiteralLength( key, keyLength, "ip" ) ) {
			char *ip;
			CheckError( decodeURLString( value, valueLength, &ip ) < 1, errorCode = AnnounceError_malformedIP );
			CheckError( CompactAddress_fromString( announce->compact, ip, NULL), errorCode = AnnounceError_malformedIP );
			seenFields |= SeenFieldOffset_IP;

		// Optional fields according to BEP7[1], I don't know if clients
		// tend to send these or not. Not supported for now.
		// [1]: http://bittorrent.org/beps/bep_0007.html#announce-parameter
		} else if ( !(seenFields & SeenFieldOffset_IPv4) && EqualLiteralLength( key, keyLength, "ipv4" ) ) {
			CheckError( handleIPv4( announce, value, valueLength ), errorCode = AnnounceError_malformedIPv4 );
			seenFields |= SeenFieldOffset_IPv4;

		} else if ( !(seenFields & SeenFieldOffset_IPv6) && EqualLiteralLength( key, keyLength, "ipv6" ) ) {
			CheckError( handleIPv6( announce, value, valueLength ), errorCode = AnnounceError_malformedIPv6 );
			seenFields |= SeenFieldOffset_IPv6;

		} else if ( !(seenFields & SeenFieldOffset_port) && EqualLiteralLength( key, keyLength, "port" ) ) {
			dbg_info( "port: %.*s", (int)valueLength, value );
			unsigned long port = strtoul( value, NULL, 10 );
			dbg_info( "portlong: %lu", port );
			CheckError( (port < 1) || (port > 65535), errorCode = AnnounceError_malformedPort );
			CompactAddress_setPort( announce->compact, (uint16_t)port );
			seenFields |= SeenFieldOffset_port;

		} else if ( !(seenFields & SeenFieldOffset_left) && EqualLiteralLength( key, keyLength, "left" ) ) {
			dbg_info( "left: %.*s", (int)valueLength, value );
			announce->left = strtoull( value, NULL, 10 );
			seenFields |= SeenFieldOffset_left;

		} else if ( !(seenFields & SeenFieldOffset_event) && EqualLiteralLength( key, keyLength, "event" ) ) {
			if ( EqualLiteralLength( value, valueLength, "started" ) )
				announce->event = AnnounceEvent_start;
			else if ( EqualLiteralLength( value, valueLength, "completed" ) )
				announce->event = AnnounceEvent_complete;
			else if ( EqualLiteralLength( value, valueLength, "stopped" ) ) {
				announce->event = AnnounceEvent_stop;
				// just hang up.
				return AnnounceError_okay;
			} else
				announce->event = AnnounceEvent_unknown;

			seenFields |= SeenFieldOffset_event;
		}
	}
	// According to BEP23, not supporting non-compact responses is
	// allowed: http://bittorrent.org/beps/bep_0023.html

	CheckError( (seenFields & SeenFieldOffset_required) != SeenFieldOffset_required, errorCode = AnnounceError_missingField );

	return AnnounceError_okay;

error:
	announce->errorMessage = AnnounceErrorStrings[errorCode];
	return errorCode;
}
