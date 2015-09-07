#include <stdlib.h>
#include <stddef.h> // size_t
#include <limits.h> // UCHAR_MAX
#include <ctype.h>

#include "Scrape.h"
#include "URLCommon.h"
#include "macros.h"
#include "dbg.h"

static int dualDecodeInfoHash( const char *input, size_t length, char *compactHash, char *infoHash ) {
	int c = 0, h = 0;
	for ( int i = 0; (i < length) && (c < 20) && (h < 40); i++, c++, h++ ) {
		if ( input[i] == '%' ) {
			if ( i + 2 > length )
				return -1;
			char encodedChar[3] = { input[i + 1], input[i + 2], '\0' };
			long decodedChar = strtol( encodedChar, NULL, 16 );
			if ( decodedChar < UCHAR_MAX )
				compactHash[c] = (char)decodedChar;
			else
				return -2;

			infoHash[h++] = tolower( input[i + 1] );
			infoHash[h]   = tolower( input[i + 2] );
			i += 2;
		} else {
			compactHash[c] = input[i];
			snprintf( infoHash + h++, 3, "%02x", input[i] );
		}
	}
	// don't null terminate compactHash.
	infoHash[h] = '\0';
	return h;
}

ScrapeData *ScrapeData_new( void ) {
	ScrapeData *scrape = malloc( sizeof(*scrape) );
	if ( !scrape ) return NULL;

	scrape->next = NULL;
	return scrape;
}

void ScrapeData_free( ScrapeData *scrape ) {
	ScrapeData *scratch = scrape;
	while ( scratch ) {
		scrape = scratch->next;
		free( scratch );
		scratch = scrape;
	}
}

void ScrapeData_dump( ScrapeData *scrape ) {
	int i = 0;
	while ( scrape ) {
		dbg_info( "Info hash %d: %s", i++, scrape->infoHash );
		scrape = scrape->next;
	}
}

typedef struct _ScrapeCallback ScrapeCallbackData;
struct _ScrapeCallback {
	ScrapeData *top;
	ScrapeData *last;
};

static int ScrapeData_parse( void *data, const char *key, size_t keyLength, const char *value, size_t valueLength ) {
	if ( EqualLiteralLength( key, keyLength, "info_hash" ) ) {
		ScrapeCallbackData *cbd = data;
		if ( !cbd->top ) {
			cbd->top = ScrapeData_new( );
			if ( !cbd->top ) return ScrapeError_unknown;
			cbd->last->next = cbd->top;
		}

		if ( dualDecodeInfoHash( value, valueLength, cbd->top->compactHash, cbd->top->infoHash ) != 40 )
			return ScrapeError_malformedInfoHash;

		// ugh.
		cbd->last = cbd->top;
		cbd->top  = cbd->last->next;
	} else
		return ScrapeError_invalidRequest;

	return ScrapeError_okay;
}

ScrapeError ScrapeData_fromQuery( ScrapeData *scrape, const char *query, size_t queryLength ) {
	ScrapeCallbackData data = { .top = scrape };
	int e = parseQueryString( query, queryLength, ScrapeData_parse, &data );
	if ( e ) return e;
	// this will only be true if no info_hash keys are encountered in the
	// query.
	if ( data.last == NULL ) return 1;
	ScrapeData_dump( scrape );
	return ScrapeError_okay;
}
