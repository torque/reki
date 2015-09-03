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
// 0:  metadata byte
// 1:   2-byte bencode string indicator `6:`.
// 3:   4-byte ipv4 address.
// 7:   2-byte ipv4 port.
// 9:   3-byte bencode string indicator `18:`.
// 12: 16-byte ipv6 address.
// 28:  2-byte ipv6 port.
#define CompactAddress_MetadataOffset     0
#define CompactAddress_IPv4BencodeOffset  1
#define CompactAddress_IPv4AddressOffset  3
#define CompactAddress_IPv4PortOffset     7
#define CompactAddress_IPv6BencodeOffset  9
#define CompactAddress_IPv6AddressOffset 12
#define CompactAddress_IPv6PortOffset    28

#define CompactAddress_IPv4Size  8
#define CompactAddress_IPv6Size 21
#define CompactAddress_Size     30

typedef enum _CompactError CompactError;

enum _CompactError {
	CompactError_okay = 0,
	CompactError_unknownAddressFamily,
	CompactError_getaddrinfoFailed,
	CompactError_getpeernameFailed,
};

void CompactAddress_init( char *compact );
CompactError CompactAddress_fromString( char *compact, const char *address, const char *port );
CompactError CompactAddress_fromSocket( char *compact, struct sockaddr_storage *socket, bool hasPort );
