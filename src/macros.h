#pragma once

#define checkConstructor( constructor ) if ( constructor == NULL ) { log_err( #constructor " failed."); return 1; }
#define checkFunction( function ) if ( function ) { log_err( #function " failed." ); return 1; }
#define EqualLiteral( in, lit ) (strncmp( in, lit, strlen( lit ) ) == 0)
