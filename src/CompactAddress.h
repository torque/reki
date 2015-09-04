#pragma once

#include <stdbool.h>
#include <sys/socket.h> // sockaddr_storage

#define CompactAddress_IPv4Flag     1
#define CompactAddress_IPv6Flag     2
#define CompactAddress_IPv4PortFlag 4
#define CompactAddress_IPv6PortFlag 8

// 6 bytes for ipv4 + port, 18 bytes for ipv6+port, 1 byte for
// signaling which address(es) are stored. All network byte order.
// Plus 5 bytes overhead for bencoded `6:` and `18:`.
// Offsets:
//  0: metadata byte
//  1:  4-byte ipv4 address.
//  5:  2-byte ipv4 port.
//  7: 16-byte ipv6 address.
// 28:  2-byte ipv6 port.
#define CompactAddress_MetadataOffset     0
#define CompactAddress_IPv4AddressOffset  1
#define CompactAddress_IPv4PortOffset     5
#define CompactAddress_IPv6AddressOffset  7
#define CompactAddress_IPv6PortOffset    23

#define CompactAddress_IPv4Size  6
#define CompactAddress_IPv6Size 18
#define CompactAddress_Size     25

typedef enum _CompactError CompactError;

enum _CompactError {
	CompactError_okay = 0,
	CompactError_unknownAddressFamily,
	CompactError_getaddrinfoFailed,
	CompactError_getpeernameFailed,
};

void CompactAddress_init( char *compact );
void CompactAddress_dump( char *compact );
CompactError CompactAddress_setPort( char *compact, uint16_t port );
CompactError CompactAddress_fromString( char *compact, const char *address, const char *port );
CompactError CompactAddress_fromSocket( char *compact, struct sockaddr_storage *socket, bool hasPort );
