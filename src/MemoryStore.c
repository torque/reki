#include <stdint.h>
#include <stdarg.h>
#include <uv.h>
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>

#include "MemoryStore.h"
#include "StringBuffer.h"
#include "CompactAddress.h"
#include "announce.h"
#include "Scrape.h"
#include "dbg.h"

struct _MemoryStore {
	redisAsyncContext *context;
	uv_timer_t *timer;
	char *namespace;
	uint64_t cleanupTime;
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
	MemoryStore *store = malloc( sizeof(*store) );
	store->namespace = strdup( namespace );
	store->timer = malloc( sizeof(*store->timer) );
	store->timer->data = store;
	return store;
}

void MemoryStore_free( MemoryStore *store ) {
	free( store->namespace );
	free( store->timer );
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

// 30 mins
#define AnnounceInterval 1800
#define DropCount 3
#define DropIntervalMS (AnnounceInterval * DropCount * 1000)
static void MemoryStore_cleanPeersTimer( uv_timer_t *timer );

int MemoryStore_attachToLoop( MemoryStore *store, uv_loop_t *loop ) {
	redisLibuvAttach( store->context, loop );
	redisAsyncSetConnectCallback( store->context, redisConnectCb );
	redisAsyncSetDisconnectCallback( store->context, redisDisconnectCb );
	uv_timer_init( loop, store->timer );
	uv_timer_start( store->timer, MemoryStore_cleanPeersTimer, DropIntervalMS, DropIntervalMS );
	return 0;
}

// This, fairly obviously, does not scale well to millions of torrents.
// However, purging the seeds/peers on every single announce/scrape
// obviously didn't scale to millions of peers either, and since a
// single torrent is more than likely to have multiple peers, this
// presumably chews up a lot less cycles overall. The tradeoff is one
// big chugging cleanup task, that could potentially stall redis, cause
// huge memory spikes, etc. A better solution might consist of
// splitting up the torrent index across several fixed-size keys, and
// staggering the cleanup tasks across those. An alternate option would
// be to use SSCAN to iterate over the keys in the set in fixed-size
// chunks.
static void MemoryStore_cleanPeers( redisAsyncContext *context, void *voidReply, void *voidStore ) {
	dbg_info( "cleanPeers" );
	redisReply *reply = voidReply;
	if ( reply->elements == 0 ) return;
	MemoryStore *store = voidStore;
	uint64_t then = store->cleanupTime - DropIntervalMS;
	redisAsyncCommand( context, NULL, NULL, "MULTI" );
	for ( int i = 0; i < reply->elements; i++ ) {
		const char *infoHash = reply->element[i]->str;
		redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:seeds 0 %llu", store->namespace, infoHash, then );
		redisAsyncCommand( context, NULL, NULL, "ZREMRANGEBYSCORE %s:%s:peers 0 %llu", store->namespace, infoHash, then );
	}
	redisAsyncCommand( context, NULL, NULL, "EXEC" );
}

static void MemoryStore_cleanPeersTimer( uv_timer_t *timer ) {
	MemoryStore *store = timer->data;
	store->cleanupTime = uv_now( timer->loop );
	redisAsyncCommand( store->context, MemoryStore_cleanPeers, store, "SMEMBERS %s:torrents", store->namespace );
}

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

	// According to BEP23, only supporting compact responses is allowed:
	// http://bittorrent.org/beps/bep_0023.html
	StringBuffer *bencode = StringBuffer_new( );
	StringBuffer_sprintf( bencode, "d8:completei%llde10:incompletei%llde8:intervali%de5:peers%lu:", seedCount, peerCount, AnnounceInterval, peerBuf->size );
	StringBuffer_join( bencode, peerBuf );
	StringBuffer_sprintf( bencode, "6:peers6%lu:", peerBuf6->size );
	StringBuffer_join( bencode, peerBuf6 );
	StringBuffer_append( bencode, "e", 1 );
	StringBuffer_free( peerBuf );
	StringBuffer_free( peerBuf6 );

	StringBuffer_sprintf( client->writeBuffer, "%d\r\n\r\n", bencode->size );
	StringBuffer_join( client->writeBuffer, bencode );
	StringBuffer_free( bencode );

	if ( announce->left == 0 )
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:seeds %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, (size_t)CompactAddress_Size );
	else
		redisAsyncCommand( context, NULL, NULL, "ZADD %s:%s:peers %llu %b", client->server->memStore->namespace, announce->infoHash, announce->score, announce->compact, (size_t)CompactAddress_Size );

	Client_reply( client );
}

void MemoryStore_processAnnounce( MemoryStore *store, ClientConnection *client ) {
	ClientAnnounceData *announce = client->request.announce;
	uint64_t then = announce->score - DropIntervalMS;
	redisAsyncCommand( store->context, NULL, NULL, "MULTI" );

	// Used for complete and incomplete fields in response. May be a bit
	// inaccurate, since old peer pruning is run in its own separate loop.
	// I doubt this is really a problem.
	redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:seeds", store->namespace, announce->infoHash );
	redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:peers", store->namespace, announce->infoHash );

	redisAsyncCommand( store->context, NULL, NULL, "ZREVRANGEBYSCORE %s:%s:seeds %llu %llu LIMIT 0 %d", store->namespace, announce->infoHash, announce->score, then, announce->numwant );
	redisAsyncCommand( store->context, NULL, NULL, "ZREVRANGEBYSCORE %s:%s:peers %llu %llu LIMIT 0 %d", store->namespace, announce->infoHash, announce->score, then, announce->numwant );

	redisAsyncCommand( store->context, MemoryStore_backendAnnounceResponse, client, "EXEC" );
}

static void MemoryStore_backendScrapeResponse( redisAsyncContext *context, void *voidReply, void *voidClient ) {
	dbg_info( "backendScrapeResponse" );
	if ( !voidReply || !voidClient ) {
		log_err( "What2???" );
		return;
	}

	redisReply *reply = voidReply;
	ClientConnection *client = voidClient;
	if ( reply->type != REDIS_REPLY_ARRAY ) {
		Client_replyErrorLen( client, "A database error occurred." );
		return;
	}

	ScrapeData *scrape = client->request.scrape;
	StringBuffer *bencode  = StringBuffer_new( );
	StringBuffer_sprintf( bencode, "d5:filesd" );
	for ( int i = 0; i < reply->elements; i += 2, scrape = scrape->next ) {
		if ( reply->element[i]->type == REDIS_REPLY_ERROR || reply->element[i+1]->type == REDIS_REPLY_ERROR ) {
			Client_replyErrorLen( client, "A database error occurred." );
			return;
		}
		long long complete   = reply->element[i]->integer;
		long long incomplete = reply->element[i+1]->integer;
		StringBuffer_append( bencode, "20:", 4 );
		StringBuffer_append( bencode, scrape->compactHash, 20 );
		StringBuffer_sprintf( bencode, "d8:completei%llde10:downloadedi0e10:incompletei%lldee", complete, incomplete );
	}

	StringBuffer_append( bencode, "ee", 2 );
	StringBuffer_sprintf( client->writeBuffer, "%d\r\n\r\n", bencode->size );
	StringBuffer_join( client->writeBuffer, bencode );
	StringBuffer_free( bencode );
	Client_reply( client );
}

void MemoryStore_processScrape( MemoryStore *store, ClientConnection *client ) {
	ScrapeData *scrape = client->request.scrape;
	redisAsyncCommand( store->context, NULL, NULL, "MULTI" );
	while ( scrape ) {
		redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:seeds", store->namespace, scrape->infoHash );
		redisAsyncCommand( store->context, NULL, NULL, "ZCARD %s:%s:peers", store->namespace, scrape->infoHash );
		scrape = scrape->next;
	}
	redisAsyncCommand( store->context, MemoryStore_backendScrapeResponse, client, "EXEC" );
}
