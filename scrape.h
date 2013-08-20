#ifndef scrape_h
#define scrape_h

#include "common.h"

typedef struct {
	char **info_hashes;
	int num;
	client_socket_data *socket_data;
} tracker_scrape_data;

static const char *scrape_base_url = "/scrape?";

void free_scrape_data(tracker_scrape_data *scrape_data);
void send_scrape_reply(redisAsyncContext *redis, void *r, void *s);
void scrape(tracker_scrape_data *scrape_data);
void parse_scrape_request(client_socket_data *data);

#endif
