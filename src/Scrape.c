#include <stdlib.h>
#include <stddef.h> // size_t

#include "Scrape.h"
#include "URLCommon.h"
#include "macros.h"
#include "dbg.h"

ScrapeData *ScrapeData_new( void ) {
	ScrapeData *scrape = malloc( sizeof(*scrape) );
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
		dbg_info( "Info hash %d: %.*s", i++, 40, scrape->infoHash );
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
			cbd->last->next = cbd->top;
		}

		if ( decodeInfoHash( value, valueLength, cbd->top->infoHash ) != 40 )
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
	ScrapeData_dump( scrape );
	return ScrapeError_okay;
}
