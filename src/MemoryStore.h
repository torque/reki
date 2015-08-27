#pragma once

typedef struct _MemoryStore MemoryStore;

#include "client.h"

MemoryStore *MemoryStore_new( const char *namespace );
void MemoryStore_free( MemoryStore *store );
int  MemoryStore_initConnection( MemoryStore *store, const char *host, short port );
int  MemoryStore_attachToLoop( MemoryStore *store, uv_loop_t *loop );
int  MemoryStore_disconnect( MemoryStore *store );

void MemoryStore_processAnnounce( MemoryStore *store, ClientConnection *client );
