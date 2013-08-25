#include "scrape.h"

static const char *info_hash_str = "info_hash";

void free_scrape_data(tracker_scrape_data *scrape_data) {
	int i;
	for (i = 0; i < scrape_data->num; i++) {
		free(scrape_data->info_hashes[i]);
	}
	free(scrape_data->info_hashes);
	free(scrape_data);
}

//d5:filesd20:....................d8:completei5e10:downloadedi50e10:incompletei10eeee
void send_scrape_reply(redisAsyncContext *redis, void *r, void *s) {
	redisReply *reply = r;
	if (reply == NULL) { return; }
	tracker_scrape_data *scrape_data = s;
	client_socket_data *data = scrape_data->socket_data;
	dynamic_string *scrape_reply = dynamic_string_init();
	dynamic_string_append(scrape_reply, "d5:filesd", 9);
	char hsh[20];
	char d1[23];
	char d2[100];
	for (int i = 0; i < scrape_data->num; i++) {
		hex_to_string(hsh, scrape_data->info_hashes[i]);
		memcpy(d1, "20:", 3);
		memcpy(d1 + 3, hsh, 20);
		dynamic_string_append(scrape_reply, d1, 23);
		long long number_of_seeds = reply->element[4*i + 2]->integer;
		long long number_of_peers = reply->element[4*i + 3]->integer;
		sprintf(d2, "d8:completei%llde10:downloadedi0e10:incompletei%lldee", number_of_seeds, number_of_peers);
		dynamic_string_append(scrape_reply, d2, strlen(d2));
	}
	dynamic_string_append(scrape_reply, "ee", 2);
	int reply_size_length = intlength(scrape_reply->size);
	int http_response_length = 82 + scrape_reply->size + reply_size_length;
	char *http_response = malloc(http_response_length*sizeof(char));
	sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", scrape_reply->size);
	memcpy(http_response + 82 + reply_size_length, scrape_reply->str, scrape_reply->size);
	int retval = send(data->sock, http_response, http_response_length, 0);
	if(retval == -1) {
		printf("%s\n", strerror(errno));
	}
	free(http_response);
	dynamic_string_free(scrape_reply);
	free_scrape_data(scrape_data);
	data->shouldfree = 1;
}

void scrape(tracker_scrape_data *scrape_data) {
	unsigned long long now = (unsigned long long)time(0) * 1000;
	unsigned long long then = now - ANNOUNCE_INTERVAL * DROP_COUNT * 1000;
	redisAsyncCommand(redis, NULL, NULL, "MULTI");
	for (int i = 0; i < scrape_data->num; i++) {
		// prune out old entries (peers that haven't announced within DROP_COUNT announce intervals)
		dbg_info("Scrape: torrent:%s", scrape_data->info_hashes[i]);
		redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:seeds 0 %llu", scrape_data->info_hashes[i], then);
		redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:peers 0 %llu", scrape_data->info_hashes[i], then);
		// used for complete and incomplete fields in response
		redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:seeds", scrape_data->info_hashes[i]);
		redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:peers", scrape_data->info_hashes[i]);
	}
	redisAsyncCommand(redis, send_scrape_reply, scrape_data, "EXEC");
}

void parse_scrape_request(client_socket_data *data) {
	tracker_scrape_data *scrape_data = calloc(1, sizeof(tracker_scrape_data));
	scrape_data->socket_data = data;
	scrape_data->info_hashes = malloc(sizeof(char*));
	scrape_data->info_hashes[0] = calloc(41, sizeof(char));
	scrape_data->num = 1;
	int encount = 0;

	int beginning_of_token = strlen(scrape_base_url), middle_of_token = -1, error = 0, pos;
	for(pos = strlen(scrape_base_url); pos <= data->url->size; pos++) {
		if(pos == data->url->size || data->url->str[pos] == '&') { // end of string or new param
			int end_of_token = pos;

			if(beginning_of_token == end_of_token) {
				dbg_info("No param");
				error = 1;
			}
			if(!error & (middle_of_token == -1)) {
				dbg_info("Missing =");
				error = 1;
			}
			if(!error & (beginning_of_token == middle_of_token || middle_of_token == end_of_token - 1)) {
				dbg_info("Missing either field or value");
				error = 1;
			}

			if(!error) { // All good
				char *field = data->url->str + beginning_of_token;
				int field_size = middle_of_token - beginning_of_token;
				char *value = data->url->str + middle_of_token + 1;
				int value_size = end_of_token - middle_of_token - 1;

				if(strlen(info_hash_str) == field_size && strncmp(info_hash_str, field, strlen(info_hash_str)) == 0) {
					if (encount == scrape_data->num) {
						scrape_data->num++;
						scrape_data->info_hashes = realloc(scrape_data->info_hashes, scrape_data->num*sizeof(char *));
						scrape_data->info_hashes[encount] = calloc(41, sizeof(char));
					}
					int parsing_succeeded = parse_info_hash(scrape_data->info_hashes[encount], 40, value, value_size);
					if (parsing_succeeded == 0) {
						dbg_info("Info hash: %.*s", 40, scrape_data->info_hashes[encount]);
						encount++;
					} else {
						dbg_info("Invalid hash: %s", value);
					}
				}
			}

			// Reset
			beginning_of_token = pos + 1;
			middle_of_token = -1;
			error = 0;
		}
		else if(data->url->str[pos] == '=') {
			if(middle_of_token != -1) {
				dbg_info("Double =");
				error = 1;
			}
			middle_of_token = pos;
		}
	}
	if (scrape_data->info_hashes[0][0] == 0) {
		simple_error(scrape_data->socket_data, "Not without an info_hash.");
		dbg_warn("Bad scrape request.");
		goto error;
	}
	scrape(scrape_data);
	return;

	error:
		if(scrape_data) free_scrape_data(scrape_data);
		data->shouldfree = 1;
		return;
}
