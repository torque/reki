#include <uv.h>
#include <signal.h>
#include <stdio.h>

#include "macros.h"
#include "dbg.h"
#include "MemoryStore.h"
#include "server.h"

static void interruptCb( uv_signal_t *interrupt, int signal ) {
	puts( "" );
	log_info( "SIGINT caught. \e[1;31mQuitting\e[m." );
	MemoryStore_disconnect( interrupt->data );
	uv_stop( interrupt->loop );
}

int main ( int argc, char **argv ) {
	uv_loop_t *loop = uv_default_loop( );
	if ( !loop ) {
		log_err( "uv loop creation failed." );
		return 1;
	}

	Server *server = Server_new( "::", "9001", ServerProtocol_TCP );
	checkConstructor( server );
	checkFunction( Server_initWithLoop( server, loop ) );
	checkFunction( Server_listen( server ) );

	MemoryStore *store = MemoryStore_new( "reki2" );
	checkConstructor( store );
	checkFunction( MemoryStore_initConnection( store, "localhost", 6379 ) );
	checkFunction( MemoryStore_attachToLoop( store, loop ) );

	server->memStore = store;

	uv_signal_t interrupt;
	interrupt.data = (void*)store;
	uv_signal_init( loop, &interrupt );
	uv_signal_start( &interrupt, interruptCb, SIGINT );

	uv_run( loop, UV_RUN_DEFAULT );
	uv_loop_close( loop );

	return 0;
}
