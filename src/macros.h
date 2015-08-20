#pragma once

#define checkConstructor( result ) if ( !result ) { log_err( #result " wasn't constructed."); return 1; }

#define checkFunction( function ) if ( function ) { log_err( #function " failed." ); return 1; }

#define EqualLiteral( in, lit ) (strncmp( in, lit, strlen( lit ) ) == 0)
