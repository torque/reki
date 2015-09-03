#include <stdint.h>
#include <stdarg.h>
#include <uv.h>
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>

#include "MemoryStore.h"
#include "StringBuffer.h"
#include "dbg.h"

struct _MemoryStore {
	redisAsyncContext *context;
	char *namespace;
};

static void redisConnectCb( const redisAsyncContext *redis, int status ) {
	if ( status != REDIS_OK ) {
		log_err( "Redis error: %s", redis->errstr );
		exit( 1 );
	}
	dbg_info( "Connected to redis." );
}

static void redisDisconnectCb( const redisAsyncContext *redis, int status ) {
	if ( status != REDIS_OK ) {
		log_err( "Redis error: %s", redis->errstr );
		return;
	}
	dbg_info( "Disconnected from redis." );
}

MemoryStore *MemoryStore_new( const char *namespace ) {
	MemoryStore *store = calloc( 1, sizeof(*store) );
	store->namespace = strdup( namespace );
	return store;
}

void MemoryStore_free( MemoryStore *store ) {
	free( store->namespace );
	free( store );
}

int MemoryStore_initConnection( MemoryStore *store, const char *host, short port ) {
	store->context = redisAsyncConnect( host, port );
	if ( store->context->err ) {
		log_err( "Redis error: %s\n", store->context->errstr );
		return 1;
	}
	return 0;
}

int MemoryStore_disconnect( MemoryStore *store ) {
	redisAsyncDisconnect( store->context );
	return 0;
}

int MemoryStore_attachToLoop( MemoryStore *store, uv_loop_t *loop ) {
	redisLibuvAttach( store->context, loop );
	redisAsyncSetConnectCallback( store->context, redisConnectCb );
	redisAsyncSetDisconnectCallback( store->context, redisDisconnectCb );
	return 0;
}

// 30 mins
#define ANNOUNCE_INTERVAL 1800
#define DROP_COUNT 3

static void MemoryStore_backendAnnounceResponse( redisAsyncContext *context, void *voidReply, void *voidClient ) {
	if ( !voidReply || !voidClient ) return;

	redisReply *reply = voidReply;
	ClientConnection *client = voidClient;
	ClientAnnounceData *announce = client->request->announce;
	if ( reply->type != REDIS_REPLY_ARRAY || reply->elements != 4 ) return;

	for ( int i = 0; i < reply->elements; i++ ) {
		if ( reply->element[i]->type == REDIS_REPLY_ERROR ) return;
	}

	// now that the error checking is out of the way, sort of, grab what
	// we came for.
	long long   seedCount = reply->element[0]->integer,
	            peerCount = reply->element[1]->integer;
	redisReply *seeds = reply->element[2],
	           *peers = reply->element[3];

	StringBuffer *bencodedResponse = StringBuffer_new( );
	// I have no regrets about hardcoding most of the bencoding.
	StringBuffer_sprintf( bencodedResponse, "d8:completei%llde10:incompletei%llde8:intervali%de5:peersl", seedCount, peerCount, ANNOUNCE_INTERVAL );
	int i = 0;
	// don't give seeds to seeds.
	if ( announce->left != 0) {
		for ( ; i < seeds->elements && i < announce->numwant; i++ ) {
			// memcpy( &dummy_peer, seeds->element[i]->str, seeds->element[i]->len);
			// if (announce->compact == 0) {
			// 	dynamic_string_append(concat_peers, dummy_peer.bencoded, dummy_peer.b_length);
			// } else {
				// StringBuffer_append( concat_peers, dummy_peer.compact, 6);
			// }
		}
	}

	for ( ; i < peers->elements && i < announce->numwant; i++ ) {
		// memcpy(&dummy_peer, peers->element[i]->str, peers->element[i]->len);
		// if (announce->compact == 0) {
		// 	dynamic_string_append(concat_peers, dummy_peer.bencoded, dummy_peer.b_length);
		// } else {
			// StringBuffer_append( concat_peers, dummy_peer.compact, 6);
		// }
	}

	redisAsyncCommand( context, NULL, NULL, "MULTI" );
	// just run the remove events on a timer? no reason to call them every
	// single announce or scrape.
	redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:seeds 0 %llu", announce->infoHash, announce->score - ANNOUNCE_INTERVAL * DROP_COUNT * 1000 );
	redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:peers 0 %llu", announce->infoHash, announce->score - ANNOUNCE_INTERVAL * DROP_COUNT * 1000 );
	if ( announce->left == 0 )
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:seeds %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, CompactAddress_Size );
	else
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:peers %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, CompactAddress_Size );
	// no callback is necessary
	redisAsyncCommand( context, NULL, NULL, "EXEC" );

	// do reply
}

void MemoryStore_processAnnounce( MemoryStore *store, ClientConnection *client ) {
	ClientAnnounceData *announce = client->request->announce;
	// uint64_t now  = uv_now( client->handle->stream->loop );
	uint64_t then = announce->score - ANNOUNCE_INTERVAL * DROP_COUNT * 1000;
	redisAsyncCommand( store->context, NULL, NULL, "MULTI" );
	// prune out old entries (peers that haven't announced within DROP_COUNT announce intervals)
	// redisAsyncCommand( store->context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:seeds 0 %llu", announce->infoHash, then );
	// redisAsyncCommand( store->context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:peers 0 %llu", announce->infoHash, then );

	// Used for complete and incomplete fields in response. May be a bit
	// inaccurate, since old peer pruning is saved until after the
	// response to reduce reply latency. I doubt this is really a problem.
	redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:seeds", store->namespace, announce->infoHash );
	redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:peers", store->namespace, announce->infoHash );

	redisAsyncCommand( store->context, NULL, NULL, "ZREVRANGEBYSCORE %s:%s:seeds %llu %llu LIMIT 0 %d", store->namespace, announce->infoHash, announce->score, then, announce->numwant );
	redisAsyncCommand( store->context, NULL, NULL, "ZREVRANGEBYSCORE %s:%s:peers %llu %llu LIMIT 0 %d", store->namespace, announce->infoHash, announce->score, then, announce->numwant );

	// do this at the same time as the old peer pruning.
	// if ( announce_data->left == 0 )
	// 	redisAsyncCommand( store->context, NULL, NULL, "ZADD %s:%s:seeds:compact %llu %b", store->namespace, announce->infoHash, now, &peer, sizeof(peer) );
	// else
	// 	redisAsyncCommand( store->context, NULL, NULL, "ZADD %s:%s:peers:compact %llu %b", store->namespace, announce->infoHash, now, &peer, sizeof(peer) );

	redisAsyncCommand( store->context, MemoryStore_backendAnnounceResponse, client, "EXEC" );
}

int MemoryStore_fetchRequestData( MemoryStore *store, ClientConnection *client ) {
	switch ( client->requestType ) {
		case ClientRequest_announce: {
			MemoryStore_processAnnounce( store, client );
			break;
		}
		case ClientRequest_scrape: {
			break;
		}
	}
	return 0;
}
