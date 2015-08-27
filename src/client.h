#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

typedef struct _ClientConnection ClientConnection;

#include "server.h"
#include "RequestParser.h"
#include "announce.h"

enum _CompactAddressOffsets {
	AddressOffset_Metadata    =  0,
	AddressOffset_IPv4Bencode =  1,
	AddressOffset_IPv4Address =  3,
	AddressOffset_IPv4Port    =  7,
	AddressOffset_IPv6Bencode =  9,
	AddressOffset_IPv6Address = 12,
	AddressOffset_IPv6Port    = 28,
	AddressOffset_Size        = 30,
};

struct _ClientConnection {
	ServerHandle *handle;
	Server *server;
	HttpParserInfo *parserInfo;
	ClientAnnounceData *announce;
	StringBuffer *readBuffer;
	StringBuffer *writeBuffer;

	// 6 bytes for ipv4 + port, 18 bytes for ipv6+port, 1 byte for
	// signaling which address(es) are stored. All network byte order.
	// Plus 5 bytes overhead for bencoded `6:` and `18:`.
	// Offsets:
	// 0:  metadata byte
	// 1:   2-byte bencoded string indicator `6:`.
	// 3:   4-byte ipv4 address.
	// 7:   2-byte ipv4 port.
	// 9:   3-byte bencoded string indicator `18:`.
	// 12: 16-byte ipv6 address.
	// 28:  2-byte ipv6 port.
	char compactAddress[AddressOffset_Size];
};

ClientConnection *ClientConnection_new( void );
void ClientConnection_free( ClientConnection *client );

int ClientConnection_getIPFromString( ClientConnection *client, const char *address, const char *port );
int ClientConnection_getIPFromSocket( ClientConnection* client );
