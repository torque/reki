#pragma once

typedef struct _ScrapeData ScrapeData;
typedef enum _ScrapeError ScrapeError;

struct _ScrapeData {
	char infoHash[40];
	ScrapeData *next;
};

enum _ScrapeError {
	ScrapeError_okay = 0,
	ScrapeError_invalidRequest,
	ScrapeError_malformedInfoHash,
};

ScrapeData *ScrapeData_new( void );
void ScrapeData_free( ScrapeData *scrape );
ScrapeError ScrapeData_fromQuery( ScrapeData *scrape, const char *query, size_t queryLength );
