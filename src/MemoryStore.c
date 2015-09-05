#include <stdint.h>
#include <stdarg.h>
#include <uv.h>
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>

#include "MemoryStore.h"
#include "StringBuffer.h"
#include "CompactAddress.h"
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
	dbg_info( "backendAnnounceResponse" );
	if ( !voidReply || !voidClient ) {
		log_err( "WHat?????" );
		return;
	}

	redisReply *reply = voidReply;
	ClientConnection *client = voidClient;
	ClientAnnounceData *announce = client->request.announce;
	if ( reply->type != REDIS_REPLY_ARRAY || reply->elements != 4 ) {
		Client_replyError( client, "A database error occurred.", 26 );
		return;
	}

	for ( int i = 0; i < reply->elements; i++ ) {
		if ( reply->element[i]->type == REDIS_REPLY_ERROR ) {
			Client_replyError( client, "A database error occurred.", 26 );
			return;
		}
	}

	// now that the error checking is out of the way, sort of, grab what
	// we came for.
	long long   seedCount = reply->element[0]->integer,
	            peerCount = reply->element[1]->integer;
	redisReply *seeds = reply->element[2],
	           *peers = reply->element[3];

	dbg_info( "s: %zu, p: %zu", seeds->elements, peers->elements );
	StringBuffer *peerBuf  = StringBuffer_new( );
	StringBuffer *peerBuf6 = StringBuffer_new( );
	int i = 0;
	while ( i < peers->elements && i < announce->numwant ) {
		char *compact = peers->element[i]->str;
		CompactAddress_dump( compact );
		if ( compact[0] & CompactAddress_IPv4Flag ) {
			StringBuffer_append( peerBuf, compact + CompactAddress_IPv4AddressOffset, CompactAddress_IPv4Size );
		} else {//if ( announce->compact[0] & CompactAddress_IPv6Flag ) {
			StringBuffer_append( peerBuf6, compact + CompactAddress_IPv6AddressOffset, CompactAddress_IPv6Size );
		}
		i++;
	}

	// don't give seeds to seeds.
	if ( announce->left != 0) {
		int j = 0;
		while ( j < seeds->elements && i < announce->numwant ) {
			char *compact = seeds->element[j]->str;
			CompactAddress_dump( compact );
			if ( compact[0] & CompactAddress_IPv4Flag ) {
				StringBuffer_append( peerBuf, compact + CompactAddress_IPv4AddressOffset, CompactAddress_IPv4Size );
			} else {//if ( announce->compact[0] & CompactAddress_IPv6Flag ) {
				StringBuffer_append( peerBuf6, compact + CompactAddress_IPv6AddressOffset, CompactAddress_IPv6Size );
			}
			j++;
			i++;
		}
	}

	// According to BEP23, not supporting non-compact responses is
	// allowed: http://bittorrent.org/beps/bep_0023.html
	StringBuffer *bencode = StringBuffer_new( );
	StringBuffer_sprintf( bencode, "d8:completei%llde10:incompletei%llde8:intervali%de5:peers%lu:", seedCount, peerCount, ANNOUNCE_INTERVAL, peerBuf->size );
	StringBuffer_join( bencode, peerBuf );
	StringBuffer_sprintf( bencode, "6:peers6%lu:", peerBuf6->size );
	StringBuffer_join( bencode, peerBuf6 );
	StringBuffer_append( bencode, "e", 1 );
	StringBuffer_free( peerBuf );
	StringBuffer_free( peerBuf6 );

	StringBuffer_sprintf( client->writeBuffer, "%d\r\n\r\n", bencode->size );
	StringBuffer_join( client->writeBuffer, bencode );
	StringBuffer_free( bencode );

	redisAsyncCommand( context, NULL, NULL, "MULTI" );
	// just run the remove events on a timer? no reason to call them every
	// single announce or scrape.
	redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:seeds 0 %llu", client->server->memStore->namespace, announce->infoHash, announce->score - ANNOUNCE_INTERVAL * DROP_COUNT * 1000 );
	redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:peers 0 %llu", client->server->memStore->namespace, announce->infoHash, announce->score - ANNOUNCE_INTERVAL * DROP_COUNT * 1000 );
	if ( announce->left == 0 )
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:seeds %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, (size_t)CompactAddress_Size );
	else
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:peers %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, (size_t)CompactAddress_Size );
	// no callback is necessary
	redisAsyncCommand( context, NULL, NULL, "EXEC" );

	Client_reply( client );
}

void MemoryStore_processAnnounce( MemoryStore *store, ClientConnection *client ) {
	ClientAnnounceData *announce = client->request.announce;
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
