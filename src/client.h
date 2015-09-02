#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

typedef struct _ClientConnection ClientConnection;
typedef union  _ClientRequestData ClientRequestData;
typedef enum   _ClientRequestType ClientRequestType;
typedef enum   _ClientError ClientError;

#include "server.h"
#include "RequestParser.h"
#include "announce.h"

enum _ClientError {
	ClientError_okay = 0;
	ClientError_unknownAddressFamily;
	ClientError_getaddrinfoFailed;
	ClientError_getpeernameFailed;
};

typedef struct {
	int pad;
} ClientScrapeData;

#define CompactAddress_IPv4Flag 1
#define CompactAddress_IPv6Flag 2

enum _CompactAddressMetadataFlags {
	AddressMetadata_IPv4Flag = 1 << 0,
	AddressMetadata_IPv6Flag = 1 << 1,
	AddressMetadata_IPv4Port = 1 << 2,
	AddressMetadata_IPv6Port = 1 << 3,
}

enum _CompactAddressOffsets {
	AddressOffset_Metadata    =  0,
	AddressOffset_IPv4Bencode =  1,
	AddressOffset_IPv4Address =  3,
	AddressOffset_IPv4Port    =  7,
	AddressOffset_IPv4Size    =  8,
	AddressOffset_IPv6Bencode =  9,
	AddressOffset_IPv6Address = 12,
	AddressOffset_IPv6Size    = 21,
	AddressOffset_IPv6Port    = 28,
	AddressOffset_Size        = 30,
};

struct _ClientConnection {
	ServerHandle *handle;
	Server *server;
	HttpParserInfo *parserInfo;
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

	union _ClientRequestData {
		ClientAnnounceData *announce;
		ClientScrapeData *scrape;
	} *request;

	enum _ClientRequestType {
		ClientRequest_announce,
		ClientRequest_scrape,
	} requestType;

#if defined(CLIENTTIMEINFO)
	uint64_t startTime;
#endif
};

ClientConnection *Client_new( void );
void Client_free( ClientConnection *client );
void Client_handleConnection( ClientConnection *client );
void Client_terminate( ClientConnection *client );

ClientError Client_IPFromString( ClientConnection *client, const char *address, const char *port );
ClientError Client_IPFromSocket( ClientConnection* client );
