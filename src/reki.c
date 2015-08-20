#include <uv.h> // libuv
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>
#include <netinet/in.h> // struct sockaddr
#include <signal.h>     // SIGINT
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "dbg.h"
#include "server.h"
#include "../http-parser/http_parser.h"

// static void redisConnectCb( const redisAsyncContext *redis, int status ) {
// 	if ( status != REDIS_OK ) {
// 		dbg_info( "Error: %s", redis->errstr );
// 		exit( 1 );
// 	}
// 	dbg_info( "Connected to redis." );
// }

// static void redisDisconnectCb( const redisAsyncContext *redis, int status ) {
// 	if ( status != REDIS_OK ) {
// 		dbg_info( "Error: %s", redis->errstr );
// 		return;
// 	}
// 	dbg_info( "Disconnected from redis." );
// }

static void interruptCb( uv_signal_t *interrupt, int signal ) {
	puts( "" );
	log_info( "SIGINT caught. \e[1;31mQuitting\e[m." );
	// redisAsyncDisconnect( interrupt->data );
	uv_stop( interrupt->loop );
}

int main ( int argc, char **argv ) {

	uv_loop_t *loop = uv_default_loop( );
	if ( !loop ) {
		log_err( "uv loop creation failed." );
		return 1;
	}

	// redisAsyncContext *redis = redisAsyncConnect( "localhost", 6379 );
	// if ( redis->err ) {
	// 	log_err( "Redis error: %s\n", redis->errstr );
	// 	free( redis );
	// 	return 1;
	// }

	uv_tcp_t server;
	int e = createServer( loop, &server );
	if ( e ) {
		log_err( "createServer failed." );
		return 1;
	}

	uv_signal_t interrupt;
	// interrupt.data = (void*)redis;
	uv_signal_init( loop, &interrupt );
	uv_signal_start( &interrupt, interruptCb, SIGINT );

	// redisLibuvAttach( redis, loop );
	// redisAsyncSetConnectCallback( redis, redisConnectCb );
	// redisAsyncSetDisconnectCallback( redis, redisDisconnectCb );
	uv_run( loop, UV_RUN_DEFAULT );
	uv_loop_close( loop );

	return 0;
}
