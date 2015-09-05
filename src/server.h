#pragma once
#include <uv.h>

typedef struct _Server Server;
typedef union _uvServerHandle ServerHandle;
typedef enum _ServerProtocol ServerProtocol;

union _uvServerHandle {
	uv_tcp_t *tcpHandle;
	uv_udp_t *udpHandle;
	uv_stream_t *stream;
};

#include "MemoryStore.h"

struct _Server {
	enum _ServerProtocol {
		ServerProtocol_TCP,
		ServerProtocol_UDP
	} protocol;

	const char *bindIP;
	const char *bindPort;
	short ipFamily;
	MemoryStore *memStore;
	ServerHandle handle;
};

Server *Server_new( const char *bindIP, const char *port, ServerProtocol type );
int Server_initWithLoop( Server *server, uv_loop_t *loop );
int Server_listen( Server *server );
