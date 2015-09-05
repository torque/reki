#pragma once

#include <string.h> // strlen

#define checkConstructor( result ) if ( !result ) { log_err( #result " wasn't constructed."); return 1; }

#define checkFunction( function ) if ( function ) { log_err( #function " failed." ); return 1; }

#define EqualLiteral( in, lit ) (strncmp( in, lit, strlen( lit ) ) == 0)
#define EqualLiteralLength( in, inlen, lit ) ((strlen(lit) == inlen) && EqualLiteral( in, lit ))

#if defined( CLIENTTIMEINFO )
	#define checktime( client, id ) fprintf( stderr, "\e[1;34m>>\e[m " id " time elapsed: %llums\n", (uv_now( client->handle.stream->loop ) - client->startTime))
#else
	#define checktime( client, id )
#endif
