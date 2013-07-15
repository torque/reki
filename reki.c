#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ev.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libev.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "dbg.h"
#include "dynamic_string.h"
#include "http-parser/http_parser.h"

#define PORT 9001
#define READSIZE 1024
#define REDIS_PORT 6379
#define ANNOUNCE_INTERVAL 1800
#define MIN_INTERVAL 60
#define DROP_COUNT 3

typedef struct {
	struct ev_loop *loop;
	ev_io *watcher;
	int sock;
	uint32_t peer_ip;
	http_parser *parser;
	http_parser_settings parser_settings;
	dynamic_string *url;
	unsigned char shouldfree;
} client_socket_data;

typedef struct {
	char peer_id[20];
	char info_hash[40];
	uint8_t port;
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
	char bencoded[68]; // maximum possible length
	char compact[6];
	// char ip_6[40];
} peer_entry;

static const char *announce_base_url = "/announce?";
static const char *scrape_base_url = "/scrape?";
redisAsyncContext *redis; // abuse scope for ease-of-use

int intlength(int input) {
	int length = 1;
	for(int i = 10; i < 10000000000; i*=10) {
		if(input < i) {
			return length;
		}
		length++;
	}
	return 10;
}

long long read_int(char *str, int str_size, const int base) {
	check(str_size <= 99, "Value is way too big, skipping.")

	char temp[100];
	memcpy(temp, str, str_size);
	temp[str_size] = '\0';

	char *endptr;
	long long num = strtoll(temp, &endptr, base);

	return num;

	error:
		return -1;
}

/* The info hash is stored in mongo, which is not binary string safe apparently,
so it needs to be parsed to a hexadecimal string rather than a binary one. */
int parse_info_hash(char *output, int output_length, char *input, int input_length) {
	int pos, i = 0, val;
	for(pos = 0; pos < input_length; pos++) {
		check(i <= output_length, "Hash is invalid (too long).")
		if (input[pos] == '%') {
			check(pos + 2 < input_length, "Hash is invalid (malformed).")
			input[pos + 1] = tolower(input[pos + 1]); // ensure lower case output
			input[pos + 2] = tolower(input[pos + 2]);
			memcpy(output + i, input + pos + 1, 2);
			pos += 2;
		} else {
			sprintf(output + i, "%02x", (int)input[pos]);
		}
		i += 2;
	}
	check(i == output_length, "Hash is invalid (too short).")
	return 0;

	error:
		return -1;
}

/* Peer id is only ever used with redis, which is binary string safe. */
int parse_peer_id(char *output, char *input, int input_length) {
	char *dummy = malloc(sizeof(char)*input_length);
	check_mem(dummy);
	int pos, i = 0;
	for(pos = 0; pos < input_length; pos++) {
		if (input[pos] == '%') {
			check(pos + 2 < input_length, "String is improperly escaped.")

			long long temp = read_int(input + pos + 1, 2, 16);
			check(temp != -1, "Failed to parse %.*s as a number.", 2, input + pos * 2);

			memcpy(dummy + i, &temp, 1);
			pos += 2;
		} else {
			memcpy(dummy + i, input + pos, 1);
		}
		pos++;
		i++;
	}
	output = dummy;
	return 0;

	error:
		if(dummy) free(dummy);
		return -1;
}

/* Input is hex string containing only [0-9A-F]. It is also only used internally,
so we don't have to worry about malicious formatting here.
Every 2 bytes in the input string correspond to 1 in the output. */
int hex_to_string(char *output, char *input) {
	int pos,
	    input_length = strlen(input),
	    output_length = input_length/2;
	char *dummy = malloc(sizeof(char)*output_length);
	check_mem(dummy);

	for (pos = 0; pos < output_length; pos++ ) {
		long long temp = read_int(input + pos * 2, 2, 16);
		check(temp != 1, "Failed to parse %.*s as a number.", 2, input + pos * 2);
		memcpy(dummy + pos, &temp, 1); // this is probably horrible
	}
	output = dummy;
	return 0;

	error:
		if(dummy) free(dummy);
		return -1;
}

int simple_error(tracker_announce_data *announce_data, char *message) {
	client_socket_data *data = announce_data->socket_data;

	int message_length = strlen(message);
	int failure_reason_length = 20 + message_length + intlength(message_length);
	int http_response_length = 82 + failure_reason_length + intlength(failure_reason_length);

	char *failure_reason = malloc(sizeof(char)*(failure_reason_length+1));
	check_mem(failure_reason);
	char *http_response = malloc(sizeof(char)*(http_response_length+1));
	check_mem(http_response);

	sprintf(failure_reason,"d14:failure reason%u:%se", message_length, message);
	sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s", failure_reason_length, failure_reason);

	send(data->sock, http_response, http_response_length, 0);
	data->shouldfree = 1;

	free(failure_reason);
	free(http_response);
	return 0;

	error:
		if(failure_reason) free(failure_reason);
		if(http_response) free(http_response);
		return -1;
}

void send_reply(redisAsyncContext *redis, void *r, void *a) {
	redisReply *reply = r;
	if (reply == NULL) { return; }
	debug("added: %lld", reply->element[6]->integer);
	tracker_announce_data *announce_data = a;
	client_socket_data *data = announce_data->socket_data;
	// reply order: rem seeds, rem peers, seeds, peers,
	redisReply *seeds = reply->element[4],
	           *peers = reply->element[5];
	int number_of_seeds = (int)reply->element[2]->integer,
	    number_of_peers = (int)reply->element[3]->integer;
  debug("seeds: %d", number_of_seeds);
	peer_entry dummy_peer;
	memset(&dummy_peer, 0, sizeof(peer_entry));
	dynamic_string *tracker_reply = dynamic_string_init();
	dynamic_string *concat_peers = dynamic_string_init();
	int dummy_length = 51 + intlength(number_of_seeds) + intlength(number_of_peers);
	char *dummy = malloc(dummy_length*sizeof(char));
	sprintf(dummy, "d8:completei%de10:incompletei%de8:intervali%de5:peers", number_of_seeds, number_of_peers, ANNOUNCE_INTERVAL);
	dynamic_string_append(tracker_reply, dummy, dummy_length);
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
		debug("cpct: %d%d%d%d%d%d", dummy_peer.compact[0],dummy_peer.compact[1],dummy_peer.compact[2],dummy_peer.compact[3],dummy_peer.compact[4],dummy_peer.compact[5]);
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
		// unsigned int len = 6*i;
		unsigned int prefix_len = intlength(concat_peers->size) + 1;
		char *dummy = malloc(prefix_len*sizeof(char));
		sprintf(dummy, "%u:", prefix_len);
		dynamic_string_append(tracker_reply, dummy, prefix_len);
		dynamic_string_join(tracker_reply, concat_peers);
		dynamic_string_append(tracker_reply, "e", 1);
	}
	int reply_size_length = intlength(tracker_reply->size);
	int http_response_length = 82 + tracker_reply->size + reply_size_length;
	char *http_response = malloc(http_response_length*sizeof(char));
	sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", tracker_reply->size);
	memcpy(http_response + 82 + reply_size_length, tracker_reply->str, tracker_reply->size);
	// fwrite(http_response,http_response_length,1,stdout);
	printf("\n");
	send(data->sock, http_response, http_response_length, 0);
	dynamic_string_free(tracker_reply);
	data->shouldfree = 1;
}

void announce(tracker_announce_data *announce_data) {
	log_info("announce");
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
	debug("compact: %d%d%d%d%d%d", peer.compact[0],peer.compact[1],peer.compact[2],peer.compact[3],peer.compact[4],peer.compact[5]);
	time_t now = time(0);

	redisAsyncCommand(redis, NULL, NULL, "MULTI");

	// prune out old entries (peers that haven't announced within DROP_COUNT announce intervals)
	redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:seeds 0 %ld", announce_data->info_hash, now - ANNOUNCE_INTERVAL * DROP_COUNT);
	redisAsyncCommand(redis, NULL, NULL, "ZREMRANGEBYSCORE torrent:%s:peers 0 %ld", announce_data->info_hash, now - ANNOUNCE_INTERVAL * DROP_COUNT);
	// used for complete and incomplete fields in response
	redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:seeds", announce_data->info_hash);
	redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:peers", announce_data->info_hash);

	announce_data->numwant = (announce_data->numwant <= 0 || announce_data->numwant > 50) ? 50 : announce_data->numwant;
	redisAsyncCommand(redis, NULL, NULL, "ZRANGE torrent:%s:seeds 0 %d", announce_data->info_hash, announce_data->numwant);
	redisAsyncCommand(redis, NULL, NULL, "ZRANGE torrent:%s:peers 0 %d", announce_data->info_hash, announce_data->numwant);

	if (announce_data->left == 0) {
		redisAsyncCommand(redis, NULL, NULL, "ZADD torrent:%s:seeds %ld %b", announce_data->info_hash, now, &peer, sizeof(peer));
	} else {
		redisAsyncCommand(redis, NULL, NULL, "ZADD torrent:%s:peers %ld %b", announce_data->info_hash, now, &peer, sizeof(peer));
	}

	// if (announce_data->event == 1 ) {
	// 	// increment completed count
	// }

	redisAsyncCommand(redis, send_reply, announce_data, "EXEC");
}

/* This function doesn't even need to exist if we're a pure public tracker */
// void check_redis(redisAsyncContext *redis, void *r, void *announce_data) {
// 	redisReply *reply = r;
// 	if (reply == NULL) { return; }
// 	long long number_of_seeds = reply->element[0]->integer,
// 	          number_of_peers = reply->element[1]->integer;
// 	// if (!(number_of_seeds + number_of_peers)) {
// 	// 	// async http request for existence of torrent, because mongo c driver can't async yet
// 	// } else {
// 		announce((tracker_announce_data*)announce_data);
// 	// }
// }

// callback for mongo hash existence request.
// void check_mongo() {

// }

int parse_announce_request(client_socket_data *data) {
	tracker_announce_data announce_data;
	memset(&announce_data, 0, sizeof(tracker_announce_data));
	announce_data.socket_data = data;
	announce_data.port = -1;
	announce_data.left = -1;
	announce_data.event = -1;
	announce_data.ip = -1;

	int beginning_of_token = strlen(announce_base_url), middle_of_token = -1, error = 0, pos;
	for(pos = strlen(announce_base_url); pos <= data->url->size; pos++) {
		if(pos == data->url->size || data->url->str[pos] == '&') { // end of string or new param
			int end_of_token = pos;

			if(beginning_of_token == end_of_token) {
				printf("No param\n");
				error = 1;
			}
			if(!error & (middle_of_token == -1)) {
				printf("Missing =\n");
				error = 1;
			}
			if(!error & (beginning_of_token == middle_of_token || middle_of_token == end_of_token - 1)) {
				printf("Missing either field or value\n");
				error = 1;
			}

			if(!error) { // All good
				char *field = data->url->str + beginning_of_token;
				int field_size = middle_of_token - beginning_of_token;
				char *value = data->url->str + middle_of_token + 1;
				int value_size = end_of_token - middle_of_token - 1;

				printf("%.*s = %.*s\n", field_size, field, value_size, value);

				const static char *peer_id_str = "peer_id";
				const static char *info_hash_str = "info_hash";
				const static char *port_str = "port";
				const static char *ip_str = "ip";
				const static char *left_str = "left";
				const static char *compact_str = "compact";
				const static char *event_str = "event";

				if(strlen(left_str) == field_size && strncmp(left_str, field, strlen(left_str)) == 0) {
					announce_data.left = read_int(value, value_size, 10);

				} else if(strlen(info_hash_str) == field_size && strncmp(info_hash_str, field, strlen(info_hash_str)) == 0) {
					int parsing_succeeded = parse_info_hash(announce_data.info_hash, 40, value, value_size);
					if (parsing_succeeded == 0) {
						printf("Info hash: %.*s\n", 40, announce_data.info_hash);
					} else {
						simple_error(&announce_data, "Invalid info hash.");
						log_info("Invalid hash: %s", value);
						return 0;
					}

				} else if(strlen(port_str) == field_size && strncmp(port_str, field, strlen(port_str)) == 0) {
					announce_data.port = read_int(value, value_size, 10);
					if(announce_data.port < 0 || announce_data.port > 65335) {
						simple_error(&announce_data, "Invalid port.");
						log_info("Invalid port: %.*s -> %d", value_size, value, announce_data.port);
					}

				} else if(strlen(ip_str) == field_size && strncmp(ip_str, field, strlen(ip_str)) == 0) {
					inet_pton(AF_INET, value, &(announce_data.ip));

				} else if(strlen(peer_id_str) == field_size && strncmp(peer_id_str, field, strlen(peer_id_str)) == 0) {
					parse_peer_id(announce_data.peer_id,value,value_size);

				} else if(strlen(compact_str) == field_size && strncmp(compact_str, field, strlen(compact_str)) == 0) {
					announce_data.compact = read_int(value, value_size, 10);

				} else if(strlen(event_str) == field_size && strncmp(event_str, field, strlen(event_str)) == 0) {
					const static char *started = "started";
					const static char *completed = "completed";
					if(strlen(value) == 7 && strncmp(started, value, 7) == 0) { // again, not sure if enough safety
						announce_data.event = 0; // 0 for started
					} else if(strlen(value) == 9 && strncmp(completed, value, 9) == 0) {
						announce_data.event = 1; // 1 for completed
					} else {
						announce_data.event = 2; // 2 for stopped/other things
						// should make sure that clients don't send events that aren't
						// started, stopped, or completed.
						simple_error(&announce_data, "Good for you.");
						return 0;
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
				printf("Double =\n");
				error = 1;
			}
			middle_of_token = pos;
		}
	}
	if (announce_data.info_hash[0] == 0) {
		simple_error(&announce_data, "No info_hash specified.");
		return 0;
	}
	if (announce_data.ip == -1) {
		announce_data.ip = data->peer_ip;
	}
	/* These redis checks are unnecessary for a public tracker.*/
	// redisAsyncCommand(redis, NULL, NULL, "MULTI");
	// redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:seeds", announce_data.info_hash);
	// redisAsyncCommand(redis, NULL, NULL, "ZCARD torrent:%s:peers", announce_data.info_hash);
	// redisAsyncCommand(redis, check_redis, &announce_data, "EXEC");
	announce(&announce_data);
	return 0;

	error:
		return -1;
}

static int parser_message_complete_callback(http_parser *parser) {
	client_socket_data *data = (client_socket_data*)parser->data;

	// printf("URL requested was: %.*s\n", (int)data->url->size, data->url->str);
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(data->peer_ip), ip, INET_ADDRSTRLEN);
	log_info("%s requested %.*s", ip, (int)data->url->size, data->url->str);

	if(data->url->size >= strlen(announce_base_url)) {
		if(strncmp(data->url->str, announce_base_url, strlen(announce_base_url)) == 0) {
			parse_announce_request(data);
		}
	}
	else {
		const char *reply = "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi";
		send(data->sock, reply, strlen(reply), 0);
	}

	data->shouldfree = 1;
	return 0;
}

static int parser_url_callback(http_parser *parser, const char *at, size_t length) {
	client_socket_data *data = (client_socket_data*)parser->data;
	dynamic_string_append(data->url, at, length);
	return 0;
}

static void read_callback(struct ev_loop *loop, ev_io *watcher, int revents) {
	static char buffer[READSIZE];
	ssize_t read_length;
	client_socket_data *data = (client_socket_data*)watcher->data;

	read_length = recv(watcher->fd, buffer, READSIZE, 0);
	if(read_length == -1) {
		perror("Could not read");
		return;
	}

	if(read_length == 0) {
		data->shouldfree = 1;
		//printf("Connection closed by client\n");
	}
	else {
		//printf("Read %d bytes\n", read_length);
		//printf("%.*s", read_length, buffer);
		http_parser_execute(data->parser, &(data->parser_settings), buffer, read_length);
	}

	if(data->shouldfree == 1) {
		ev_io_stop(data->loop, data->watcher);
		free(data->watcher);
		free(data->parser);
		dynamic_string_free(data->url);
		close(data->sock);
		free(data);
	}
}

static void accept_callback(struct ev_loop *loop, ev_io *watcher, int revents) {
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int client_sock = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if(client_sock == -1) {
		perror("Could not accept connection");
		return;
	}

	char address_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(client_addr.sin_addr), address_str, INET_ADDRSTRLEN);
	//printf("Connection accepted from %s\n", address_str);

	client_socket_data *data = malloc(sizeof(client_socket_data));
	data->loop = loop;
	data->watcher = (ev_io*)malloc(sizeof(ev_io));
	data->sock = client_sock;
	data->peer_ip = client_addr.sin_addr.s_addr;
	data->url = dynamic_string_init();
	data->shouldfree = 0;

	memset(&data->parser_settings, 0, sizeof(data->parser_settings));
	data->parser_settings.on_url = parser_url_callback;
	data->parser_settings.on_message_complete = parser_message_complete_callback;
	data->parser = (http_parser*)malloc(sizeof(http_parser));
	data->parser->data = (void*)data;
	http_parser_init(data->parser, HTTP_REQUEST);

	ev_io_init(data->watcher, read_callback, client_sock, EV_READ);
	data->watcher->data = data;
	ev_io_start(loop, data->watcher);
}

void sigint_callback(EV_P_ ev_signal *w, int revents)
{
		printf("\nSIGINT caught.\n");
		redisAsyncDisconnect(redis);
		ev_unloop(EV_A_ EVUNLOOP_ALL);
}

void redis_connect_callback(const redisAsyncContext *redis, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", redis->errstr);
		return;
	}
	printf("Connected to redis-server on port %d.\n",REDIS_PORT);
}

void redis_disconnect_callback(const redisAsyncContext *redis, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", redis->errstr);
		return;
	}
	printf("Disconnected from redis_server.\n");
}

int main()
{
	int retval;

	// Create socket
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1) {
		perror("Could not create socket");
		return 1;
	}

	int socketoption = 1;
	retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &socketoption, sizeof(socketoption));
	if(retval == -1) {
		perror("Could not set socket options");
		return 1;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	retval = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if(retval == -1) {
		perror("Could not bind");
		return 1;
	}

	retval = listen(sock, 1000 /*backlog*/);
	if(retval == -1) {
		perror("Could not listen");
		return 1;
	}

	struct ev_loop *loop = ev_default_loop(0);
	ev_io *accept_watcher = (ev_io*)malloc(sizeof(ev_io));
	ev_io_init(accept_watcher, accept_callback, sock, EV_READ);

	redis = redisAsyncConnect("127.0.0.1", REDIS_PORT);
	if (redis->err) {
		printf("Failed to connect to redis-server: %s\n", redis->errstr);
		redisAsyncFree(redis);
		return 1;
	}
	redisLibevAttach(EV_DEFAULT_ redis);
	redisAsyncSetConnectCallback(redis,redis_connect_callback);
	redisAsyncSetDisconnectCallback(redis,redis_disconnect_callback);

	ev_signal sigint_watcher;
	ev_signal_init(&sigint_watcher, sigint_callback, SIGINT);
	ev_signal_start(loop, &sigint_watcher);

	ev_io_start(loop, accept_watcher);
	ev_run(loop, 0);

	ev_io_stop(loop, accept_watcher);
	ev_signal_stop(loop, &sigint_watcher);
	printf("Closing connection\n");
	free(accept_watcher);
	ev_loop_destroy(loop);
	close(sock);

	return 0;
}
