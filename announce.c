#include "announce.h"

static const char *info_hash_str = "info_hash";
static const char *peer_id_str = "peer_id";
static const char *port_str = "port";
static const char *ip_str = "ip";
static const char *left_str = "left";
static const char *compact_str = "compact";
static const char *event_str = "event";

static void write_callback(struct ev_loop *loop, ev_io *watcher, int revents);
static void close_mongo_watcher(struct ev_loop *loop, ev_io *watcher);

static void close_mongo_watcher(struct ev_loop *loop, ev_io *watcher) {
	ev_io_stop(loop, watcher);
	close(watcher->fd);
	free(watcher);
}

static void write_callback(struct ev_loop *loop, ev_io *watcher, int revents) {
	int err;
	socklen_t len = sizeof(err);
	getsockopt(watcher->fd, SOL_SOCKET, SO_ERROR, &err, &len);
	if(err != 0) {
		errno = err;
		fancy_perror("Problem connecting to mongo");
		close_mongo_watcher(loop, watcher);
		return;
	}
	char *info_hash = (char*)watcher->data;
	// tracker_announce_data *announce_data = (tracker_announce_data*)watcher->data;
	char query[93];
	sprintf(query, "GET /tracker/%.*s/snatched HTTP/1.0\r\nHost: localhost\r\n\r\n", 40, info_hash);
	send(watcher->fd, query, strlen(query), 0);

	free(info_hash);
	close_mongo_watcher(loop, watcher);
}

void increment_completion_count(client_socket_data *data, char* info_hash) {
	int sock = socket(AF_INET, SOCK_STREAM, 6); // hardcoding things is totally 9001% futureproof
	if(sock == -1) {
		fancy_perror("Could not create socket");
		return;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SITE_PORT);
	addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

	int socket_flags = fcntl(sock, F_GETFL, 0);
	if(socket_flags == -1) {
		fancy_perror("Could not get socket flags");
		return;
	}
	int retval = fcntl(sock, F_SETFL, socket_flags | O_NONBLOCK);
	if(retval == -1) {
		fancy_perror("Could not set socket flags");
		return;
	}

	ev_io *write_watcher = (ev_io*)malloc(sizeof(ev_io));
	write_watcher->data = info_hash;

	ev_io_init(write_watcher, write_callback, sock, EV_WRITE);
	connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr));
}

void send_announce_reply(redisAsyncContext *redis, void *r, void *a) {
	redisReply *reply = r;
	if (reply == NULL) { return; }
	tracker_announce_data *announce_data = (tracker_announce_data*)a;
	client_socket_data *data = announce_data->socket_data;
	redisReply *seeds = reply->element[4],
	           *peers = reply->element[5];
	int number_of_seeds = (int)reply->element[2]->integer,
	    number_of_peers = (int)reply->element[3]->integer;

	peer_entry dummy_peer;
	memset(&dummy_peer, 0, sizeof(peer_entry));

	dynamic_string *tracker_reply = dynamic_string_init();
	dynamic_string *concat_peers = dynamic_string_init();
	int dummy_length = 52 + intlength(number_of_seeds) + intlength(number_of_peers);
	char *dummy = malloc(dummy_length * sizeof(char));
	check_mem(dummy);
	sprintf(dummy, "d8:completei%de10:incompletei%de8:intervali%de5:peers", number_of_seeds, number_of_peers, ANNOUNCE_INTERVAL);
	dynamic_string_append(tracker_reply, dummy, dummy_length);
	free(dummy);
	int i = 0;

	// don't give seeds to seeds.
	if (announce_data->left != 0) {
		for (; i < seeds->elements && i < announce_data->numwant; i++) {
			memcpy(&dummy_peer, seeds->element[i]->str, seeds->element[i]->len);
			if (announce_data->compact == 0) {
				dynamic_string_append(concat_peers, dummy_peer.bencoded, dummy_peer.b_length);
			} else {
				dynamic_string_append(concat_peers, dummy_peer.compact, 6);
			}
		}
	}

	for (; i < peers->elements && i < announce_data->numwant; i++) {
		memcpy(&dummy_peer, peers->element[i]->str, peers->element[i]->len);
		if (announce_data->compact == 0) {
			dynamic_string_append(concat_peers, dummy_peer.bencoded, dummy_peer.b_length);
		} else {
			dynamic_string_append(concat_peers, dummy_peer.compact, 6);
		}
	}

	if (announce_data->compact == 0) {
		dynamic_string_append(tracker_reply, "l", 1);
		dynamic_string_join(tracker_reply, concat_peers);
		dynamic_string_append(tracker_reply, "ee", 2);
	} else {
		int prefix_len = intlength(concat_peers->size) + 2;
		char *dummy = malloc(prefix_len*sizeof(char));
		sprintf(dummy, "%lu:", concat_peers->size);
		dynamic_string_append(tracker_reply, dummy, prefix_len);
		free(dummy);
		dynamic_string_join(tracker_reply, concat_peers);
		dynamic_string_append(tracker_reply, "e", 1);
	}

	int reply_size_length = intlength(tracker_reply->size);
	int http_response_length = 82 + tracker_reply->size + reply_size_length;
	char *http_response = malloc(http_response_length*sizeof(char));
	sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", tracker_reply->size);
	memcpy(http_response + 82 + reply_size_length, tracker_reply->str, tracker_reply->size);

	int retval = send(data->sock, http_response, http_response_length, 0);
	if(retval == -1) {
		fancy_perror("Could not send to socket");
	}
	free(http_response);
	free(announce_data);
	dynamic_string_free(tracker_reply);
	free_client_socket_data(data);
}

void announce(tracker_announce_data *announce_data) {
	// d2:ip15:255.255.255.2557:peer id20:123456789012345678904:porti60001ee
	peer_entry peer;
	memset(&peer, 0, sizeof(peer_entry));
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(announce_data->ip), ip, INET_ADDRSTRLEN);
	int ip_len = strlen(ip),
	    ip_len_len = intlength(ip_len);

	peer.b_length = 47 + ip_len + ip_len_len + intlength(announce_data->port);
	sprintf(peer.bencoded, "d2:ip%d:%s7:peer id20:", ip_len, ip);
	int i = 18 + ip_len + ip_len_len;
  memcpy(peer.bencoded + i, announce_data->peer_id, 20);
	sprintf(peer.bencoded + i + 20, "4:porti%dee", announce_data->port);

	// compact it, yo
	memcpy(peer.compact, &(announce_data->ip), 4);
	memcpy(peer.compact + 4, &(announce_data->port), 2);
	unsigned long long now = (unsigned long long)time(0) * 1000;
	unsigned long long then = now - ANNOUNCE_INTERVAL * DROP_COUNT * 1000;
	redisAsyncCommand(redis, NULL, NULL, "MULTI");

	// prune out old entries (peers that haven't announced within DROP_COUNT announce intervals)
	dbg_info("Redis key: torrent:%s", announce_data->info_hash);
	redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:seeds 0 %llu", announce_data->info_hash, then);
	redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:peers 0 %llu", announce_data->info_hash, then);
	// used for complete and incomplete fields in response
	redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:seeds", announce_data->info_hash);
	redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:peers", announce_data->info_hash);

	announce_data->numwant = (announce_data->numwant <= 0 || announce_data->numwant > 50) ? 50 : announce_data->numwant;
	redisAsyncCommand(redis, NULL, NULL, "ZRANGE torrent:%s:seeds 0 %d", announce_data->info_hash, announce_data->numwant);
	redisAsyncCommand(redis, NULL, NULL, "ZRANGE torrent:%s:peers 0 %d", announce_data->info_hash, announce_data->numwant);

	if (announce_data->left == 0) {
		redisAsyncCommand(redis, NULL, NULL, "ZADD torrent:%s:seeds %llu %b", announce_data->info_hash, now, &peer, sizeof(peer));
	} else {
		redisAsyncCommand(redis, NULL, NULL, "ZADD torrent:%s:peers %llu %b", announce_data->info_hash, now, &peer, sizeof(peer));
	}

	if (announce_data->event == 1 ) {
		char *info_hash = (char*)malloc(sizeof(char)*41);
		memcpy(info_hash, announce_data->info_hash, 41);
		increment_completion_count(announce_data->socket_data, info_hash);
	}
	redisAsyncCommand(redis, send_announce_reply, announce_data, "EXEC");
}

void parse_announce_request(client_socket_data *data) {
	tracker_announce_data *announce_data = calloc(1, sizeof(tracker_announce_data));
	announce_data->socket_data = data;
	announce_data->port = -1;
	announce_data->left = -1;
	announce_data->event = -1;
	announce_data->ip = -1;

	int beginning_of_token = strlen(announce_base_url), middle_of_token = -1, error = 0, pos;
	for(pos = strlen(announce_base_url); pos <= data->url->size; pos++) {
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
					int parsing_succeeded = parse_info_hash(announce_data->info_hash, 40, value, value_size);
					if (parsing_succeeded == 0) {
						dbg_info("Info hash: %.*s", 40, announce_data->info_hash);
					} else {
						simple_error(announce_data->socket_data, "Invalid info hash.");
						dbg_warn("Invalid hash: %s", value);
						goto error;
					}

				} else if(strlen(left_str) == field_size && strncmp(left_str, field, strlen(left_str)) == 0) {
					announce_data->left = read_int(value, value_size, 10);
					dbg_info("Left: %lld", announce_data->left);

				} else if(strlen(port_str) == field_size && strncmp(port_str, field, strlen(port_str)) == 0) {
					long long temp = read_int(value, value_size, 10);
					announce_data->port = (int)temp;
					if(announce_data->port < 0 || announce_data->port > 0xffff) {
						simple_error(announce_data->socket_data, "Invalid port.");
						dbg_warn("Invalid port: %.*s -> %d", value_size, value, announce_data->port);
						goto error;
					}
					dbg_info("Port: %d", announce_data->port);

				} else if(strlen(ip_str) == field_size && strncmp(ip_str, field, strlen(ip_str)) == 0) {
					inet_pton(AF_INET, value, &(announce_data->ip));
					dbg_info("IP: %d", announce_data->ip);

				} else if(strlen(peer_id_str) == field_size && strncmp(peer_id_str, field, strlen(peer_id_str)) == 0) {
					parse_peer_id(announce_data->peer_id,value,value_size);

				} else if(strlen(compact_str) == field_size && strncmp(compact_str, field, strlen(compact_str)) == 0) {
					announce_data->compact = read_int(value, value_size, 10);
					dbg_info("compact: %d", announce_data->compact);

				} else if(strlen(event_str) == field_size && strncmp(event_str, field, strlen(event_str)) == 0) {
					const static char *started = "started";
					const static char *completed = "completed";
					if(value_size == 7 && strncmp(started, value, 7) == 0) {
						announce_data->event = 0; // 0 for started
					} else if(value_size == 9 && strncmp(completed, value, 9) == 0) {
						announce_data->event = 1; // 1 for completed
					} else {
						/* stopped, assuming clients don't send other events I don't know about.
						probably would be good to remove the peer from the sorted set, but
						that would require effort. */
						simple_error(announce_data->socket_data, "Bye.");
						goto error;
					}
				}
			}

			// Reset
			beginning_of_token = pos + 1;
			middle_of_token = -1;
			error = 0;
		}
		else if(data->url->str[pos] == '=') {
			if(middle_of_token !=  -1) {
				dbg_info("Double =");
				error = 1;
			}
			middle_of_token = pos;
		}
	}
	if (announce_data->info_hash[0] == 0) {
		simple_error(announce_data->socket_data, "No info_hash specified.");
		dbg_warn("No info_hash.");
		goto error;
	}
	if (announce_data->port == -1) {
		simple_error(announce_data->socket_data, "No port specified.");
		dbg_warn("No port.");
		goto error;
	}
	if (announce_data->ip == -1) {
		announce_data->ip = data->peer_ip;
	}
	announce(announce_data);
	return;

	error:
		if (announce_data) free(announce_data);
		return;
}
