#ifndef announce_h
#define announce_h

#include "common.h"

typedef struct {
	char peer_id[20];
	char info_hash[41]; //strlen needs to work
	int port;
	long long left;
	int compact;
	int event;
	uint32_t ip;
	// uint8_t ip_6[16];
	int numwant;
	client_socket_data *socket_data;
} tracker_announce_data;

typedef struct {
	int b_length;
	char bencoded[68];
	char compact[6];
	// char ip_6[40];
} peer_entry;

static const char *announce_base_url = "/announce?";

void increment_completion_count(client_socket_data *data, char* info_hash);
void send_announce_reply(redisAsyncContext *redis, void *r, void *a);
void announce(tracker_announce_data *announce_data);
void parse_announce_request(client_socket_data *data);

#endif
