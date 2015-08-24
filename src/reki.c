#include <uv.h> // libuv
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>
#include <signal.h>     // SIGINT
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"
#include "dbg.h"
#include "server.h"

static void interruptCb( uv_signal_t *interrupt, int signal ) {
	puts( "" );
	log_info( "SIGINT caught. \e[1;31mQuitting\e[m." );
	uv_stop( interrupt->loop );
}

int main ( int argc, char **argv ) {
	uv_loop_t *loop = uv_default_loop( );
	if ( !loop ) {
		log_err( "uv loop creation failed." );
		return 1;
	}

	Server *server1 = Server_new( "::", "9001", ServerProtocol_TCP );
	checkConstructor( server1 );
	checkFunction( Server_initWithLoop( server1, loop ) );
	checkFunction( Server_listen( server1 ) );

	uv_signal_t interrupt;
	uv_signal_init( loop, &interrupt );
	uv_signal_start( &interrupt, interruptCb, SIGINT );

	uv_run( loop, UV_RUN_DEFAULT );
	uv_loop_close( loop );

	return 0;
}
