// #include <netinet/in.h> // struct sockaddr
#include <netdb.h>  // getaddrinfo
#include <string.h> // memcpy
#include <stdint.h>

#include "CompactAddress.h"
#include "dbg.h"

void CompactAddress_init( char *compact ) {
	memset( compact, 0, CompactAddress_Size );
}

CompactError CompactAddress_setPort( char *compact, uint16_t port ) {
	port = htons( port );
	if ( !(compact[0] & CompactAddress_IPv4PortFlag) )
		memcpy( compact + CompactAddress_IPv4PortOffset, &port, 2 );
	if ( !(compact[0] & CompactAddress_IPv6PortFlag) )
		memcpy( compact + CompactAddress_IPv6PortOffset, &port, 2 );
	return 0;
}

CompactError CompactAddress_fromSocket( char *compact, struct sockaddr_storage *socket, bool hasPort ) {
	switch ( socket->ss_family ) {
		case AF_INET: {
			dbg_info( "fromSocket: %08X", ntohl(((struct sockaddr_in*)socket)->sin_addr.s_addr) );
			memcpy( compact + CompactAddress_IPv4AddressOffset, &((struct sockaddr_in*)socket)->sin_addr, 4 );
			if ( hasPort )
				memcpy( compact + CompactAddress_IPv4PortOffset , &((struct sockaddr_in*)socket)->sin_port, 2 );
			compact[0] |= CompactAddress_IPv4Flag;
			break;
		}
		case AF_INET6: {
// this is pretty dumb.
#define dumb(x) ntohs(*(uint16_t*)(x + 0)), ntohs(*(uint16_t*)(x + 2)), ntohs(*(uint16_t*)(x + 4)), ntohs(*(uint16_t*)(x + 6)), ntohs(*(uint16_t*)(x + 8)), ntohs(*(uint16_t*)(x + 10)), ntohs(*(uint16_t*)(x + 12)), ntohs(*(uint16_t*)(x + 14))
			dbg_info( "fromSocket: %04X:%04X:%04X:%04X:%04X:%04X:%04X:%04X", dumb(((struct sockaddr_in6*)socket)->sin6_addr.s6_addr) );
#undef dumb
			memcpy( compact + CompactAddress_IPv6AddressOffset, &((struct sockaddr_in6*)socket)->sin6_addr, 16 );
			if ( hasPort )
				memcpy( compact + CompactAddress_IPv6PortOffset , &((struct sockaddr_in6*)socket)->sin6_port,  2 );
			compact[0] |= CompactAddress_IPv6Flag;
			break;
		}
		default: {
			log_err( "Compact_fromSocket: unknown address family: %d", socket->ss_family );
			return CompactError_unknownAddressFamily;
		}
	}

	return CompactError_okay;
}

CompactError CompactAddress_fromString( char *compact, const char *address, const char *port ) {
	CompactError status;
	struct addrinfo hints, *res;
	memset( &hints, 0, sizeof(hints) );
	// OS X manpage says PF_UNSPEC but linux says AF_UNSPEC. PF_UNSPEC is
	// defined as AF_UNSPEC on OS X, so we'll just use that. They're both
	// defined as 0.
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST;
	// uv_getaddrinfo is async. If hostname lookup is ever implemented,
	// this will probably need to be switched.
	int e = getaddrinfo( address, port, &hints, &res );
	if ( e )
		return CompactError_getaddrinfoFailed;

	status = CompactAddress_fromSocket( compact, (struct sockaddr_storage *)res->ai_addr, port != NULL );
	freeaddrinfo( res );
	return status;
}
