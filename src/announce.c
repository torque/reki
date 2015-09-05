#include <ctype.h> // tolower
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include "announce.h"
#include "dbg.h"
#include "macros.h"
#include "URLCommon.h"

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
	ClientAnnounceData *announce = malloc( sizeof(*announce) );
	announce->numwant = 20;
	announce->event   = AnnounceEvent_none;
	CompactAddress_init( announce->compact );
	announce->seenFields = 0;
	announce->infoHash = NULL;
	announce->id = NULL;
	return announce;
}

void ClientAnnounceData_free( ClientAnnounceData *announce ) {
	if ( !announce ) return;
	dbg_info( "ClientAnnounceData_free" );
	free( announce->infoHash );
	free( announce->id );
	free( announce );
}

// These break with the naming convention to allow for some preprocessor
// magic.
enum _SeenFieldOffsets {
	SeenFieldOffset_peer_id   = 1 << 0,
	SeenFieldOffset_info_hash = 1 << 1,
	SeenFieldOffset_port      = 1 << 2,
	SeenFieldOffset_event     = 1 << 3,
	SeenFieldOffset_ip        = 1 << 4,
	SeenFieldOffset_ipv4      = 1 << 5,
	SeenFieldOffset_ipv6      = 1 << 6,
	SeenFieldOffset_left      = 1 << 7,
	SeenFieldOffset_numwant   = 1 << 8,
	SeenFieldOffset_required  = SeenFieldOffset_peer_id | SeenFieldOffset_info_hash | SeenFieldOffset_port | SeenFieldOffset_left,
};

#define CheckError( boolean, err ) if ( boolean ) { return err; }
#define CheckField( fieldName ) !(announce->seenFields & SeenFieldOffset_##fieldName) && EqualLiteralLength( key, keyLength, #fieldName )
static int ClientAnnounceData_parse( void *data, const char *key, size_t keyLength, const char *value, size_t valueLength ) {
	ClientAnnounceData *announce = data;

	// Compare keys to get values. This is not particularly elegant.
	if ( CheckField( peer_id ) ) {
		dbg_info( "peer_id: %.*s", (int)valueLength, value );
		CheckError( decodeURLString( value, valueLength, &announce->id ) < 1, AnnounceError_malformedID );
		announce->seenFields |= SeenFieldOffset_peer_id;

	} else if ( CheckField( info_hash ) ) {
		announce->infoHash = malloc( 41 * sizeof(*announce->infoHash) );
		CheckError( decodeInfoHash( value, valueLength, announce->infoHash ) != 40, AnnounceError_malformedInfoHash );
		dbg_info( "info_hash: %.*s", 40, announce->infoHash );
		announce->seenFields |= SeenFieldOffset_info_hash;

	// The IP value in the request can allegedly be a DNS name,
	// according to BEP3[1]. I don't know if any clients do this, but
	// it's not currently supported.
	// [1]: http://bittorrent.org/beps/bep_0003.html#trackers
	} else if ( CheckField( ip ) ) {
		char *ip;
		CheckError( decodeURLString( value, valueLength, &ip ) < 1, AnnounceError_malformedIP );
		CheckError( CompactAddress_fromString( announce->compact, ip, NULL), AnnounceError_malformedIP );
		announce->seenFields |= SeenFieldOffset_ip;

	// Optional fields according to BEP7[1], I don't know if clients
	// tend to send these or not. Not supported for now.
	// [1]: http://bittorrent.org/beps/bep_0007.html#announce-parameter
	} else if ( CheckField( ipv4 ) ) {
		CheckError( handleIPv4( announce, value, valueLength ), AnnounceError_malformedIPv4 );
		announce->seenFields |= SeenFieldOffset_ipv4;

	} else if ( CheckField( ipv6 ) ) {
		CheckError( handleIPv6( announce, value, valueLength ), AnnounceError_malformedIPv6 );
		announce->seenFields |= SeenFieldOffset_ipv6;

	} else if ( CheckField( port ) ) {
		dbg_info( "port: %.*s", (int)valueLength, value );
		unsigned long port = strtoul( value, NULL, 10 );
		dbg_info( "portlong: %lu", port );
		CheckError( (port < 1) || (port > 65535), AnnounceError_malformedPort );
		CompactAddress_setPort( announce->compact, (uint16_t)port );
		announce->seenFields |= SeenFieldOffset_port;

	} else if ( CheckField( left ) ) {
		dbg_info( "left: %.*s", (int)valueLength, value );
		announce->left = strtoull( value, NULL, 10 );
		announce->seenFields |= SeenFieldOffset_left;

	} else if ( CheckField( event ) ) {
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

		announce->seenFields |= SeenFieldOffset_event;

	} else if ( CheckField( numwant ) ) {
		unsigned long numwant = strtoul( value, NULL, 10 );
		if ( numwant < 20 && numwant > 0 )
			announce->numwant = numwant;

		announce->seenFields |= SeenFieldOffset_numwant;
	}

	return AnnounceError_okay;
}

AnnounceError ClientAnnounceData_fromQuery( ClientAnnounceData *announce, const char *query, size_t queryLength ) {
	int e = parseQueryString( query, queryLength, ClientAnnounceData_parse, announce );
	if ( e ) {
		announce->errorMessage = AnnounceErrorStrings[e];
		return e;
	}

	if ( (announce->seenFields & SeenFieldOffset_required) != SeenFieldOffset_required ) {
		announce->errorMessage = AnnounceErrorStrings[AnnounceError_missingField];
		return AnnounceError_missingField;
	}

	return AnnounceError_okay;
}
