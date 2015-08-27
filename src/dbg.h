#pragma once

#include <stdio.h>
#include <errno.h>
#include <string.h>

#if defined( NDEBUG )
	#define dbg_info( message, ... )
	#define dbg_warn( message, ... )
#else
	#define dbg_info( message, ... ) fprintf( stderr, "\e[1;32m>>\e[m ( %s:%d )\e[1;m " message "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
	#define dbg_warn( message, ... ) fprintf( stderr, "\e[1;33m>>\e[m ( %s:%d )\e[1;m " message "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
#endif

#define log_info( message, ... ) fprintf( stderr, "\e[1;32m>>\e[m " message "\n", ##__VA_ARGS__ )
#define log_warn( message, ... ) fprintf( stderr, "\e[1;33m>>\e[m ( %s:%d ) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
#define log_err( message, ... ) fprintf( stderr, "\e[1;31m>>\e[m ( %s:%d ) " message "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
#define fancy_perror( message ) { fprintf( stderr, "\e[1;31m>>\e[m ( %s:%d ) ", __FILE__, __LINE__ ); perror( message ); }
#define check( condition, message, ... ) if( !( condition ) ) { log_warn( message, ##__VA_ARGS__ ); goto error; }
#define check_mem( mem ) if ( !mem ) { log_err( "\e[1;31mMemory allocation failed\e[m" ); exit( 1 ); }
