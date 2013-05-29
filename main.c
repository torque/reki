#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ev.h>
#include <strings.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libev.h>
#include "http-parser/http_parser.h"

#define PORT 8081
#define READSIZE 1024
#define DYNAMICSTRINGSIZE 1024
#define REDIS_PORT 6379

typedef struct {
	char *str;
	size_t size;
	size_t alloc_size;
} dynamic_string;

struct client_socket_data {
	struct ev_loop *loop;
	ev_io *watcher;
	int sock;
	http_parser *parser;
	http_parser_settings parser_settings;
	dynamic_string *url;
	unsigned char shouldfree;
};

static const char *announce_base_url = "/announce?";
redisAsyncContext *redis; // abuse scope for ease-of-use

dynamic_string *dynamic_string_init() {
	dynamic_string *str = (dynamic_string *)malloc(sizeof(dynamic_string));
	str->alloc_size = DYNAMICSTRINGSIZE;
	str->size = 0;
	str->str = (char*)malloc(sizeof(char)*str->alloc_size);
	return str;
}

void dynamic_string_free(dynamic_string *str) {
	free(str->str);
	free(str);
}

void dynamic_string_append(dynamic_string *str, char *append, size_t size) {
	while(str->size + size > str->alloc_size) {
		str->str = (char *)realloc((void *)str->str, str->alloc_size*2);
		str->alloc_size *= 2;
	}

	memcpy(((void*)str->str) + str->size, (void *)append, size);
	str->size += size;
}

struct tracker_announce_data {
	char peer_id[20];
	char info_hash[40];
	int port;
	long long left;
	int compact;
	int event;
};

long long read_int(char *str, int str_size) {
	char temp[100];
	if(str_size + 1 > 100) {
		printf("Value for left way too long, skipping\n");
		return -1;
	}
	memcpy(temp, str, str_size);
	temp[str_size] = '\0';

	errno = 0;
	char *endptr;
	long long num = strtoll(temp, &endptr, 10);
	if(errno == ERANGE) {
		printf("Left value overflowed\n");
		return -1;
	}
	else if(errno != 0 || *endptr != '\0') {
		printf("Error in string-to-int conversion\n");
		return -1;
	}

	return num;
}

void parse_info_hash(char *info_hash, char *url_info_hash, int url_info_hash_length) {
	int pos, i = 0;
	for(pos = 0; (pos < url_info_hash_length && i < 40); pos++) {
		if (url_info_hash[pos] == '%') {
			if (pos + 2 >= url_info_hash_length ) {
				return; // malformed info_hash, do something about it.
			} else {
				memcpy(info_hash + i, url_info_hash + pos + 1, 2);
				pos += 2;
			}
		} else {
			sprintf(info_hash + i, "%02x", (int)url_info_hash[pos]);
		}
		i += 2;
	}
}

void check_database(redisAsyncContext *redis, void *r, void *info_hash) {
	// if there are peers, then the torrent exists.
	// if there are no peers, check the database.
	redisReply *reply = r;
	if (reply == NULL) { return; }
	printf("Number of peers: %lld", reply->integer);
	if (!reply->integer) {
		// check database for torrent existence
	}
}

void check_peers(redisAsyncContext *redis, void *r, void *info_hash) {	// callback to check if there are any peers
	// if there are seeds, then the torrent exists.
	// if there are no seeds, check for number of peers.
	redisReply *reply = r;
	if (reply == NULL) { 
		// an error has occurred and the context needs to be destroyed as well
		return; 
	}
	printf("Number of seeds: %lld\n", reply->integer);
	if(!reply->integer) {
		redisAsyncCommand(redis, check_database, info_hash, "ZCARD %s:peers", (char*)info_hash);
	}
}

void announce(struct client_socket_data* data) {
	struct tracker_announce_data announce_data;
	bzero(&announce_data, sizeof(announce_data));
	announce_data.port = -1;
	announce_data.left = -1;
	announce_data.event = -1;

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


				const static char *left_str = "left";
				if(strlen(left_str) == field_size && strncmp(left_str, field, strlen(left_str)) == 0) {
					announce_data.left = read_int(value, value_size);
				}

				const static char *info_hash_str = "info_hash";
				if (strlen(info_hash_str) == field_size && strncmp(info_hash_str, field, strlen(info_hash_str)) == 0) {
					parse_info_hash(announce_data.info_hash, value, value_size);
					printf("Info hash: %s\n",announce_data.info_hash);
				}

				const static char *port_str = "port";
				if(strlen(port_str) == field_size && strncmp(port_str, field, strlen(port_str)) == 0) {
					announce_data.port = read_int(value, value_size);
					if(announce_data.port < 0 || announce_data.port > 65335) {
						printf("Invalid port\n");
						announce_data.port = -1;
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
	// check if torrent exists
	redisAsyncCommand(redis, check_peers, announce_data.info_hash, "ZCARD %s:seeds", announce_data.info_hash);
}

static int parser_message_complete_callback(http_parser *parser) {
	struct client_socket_data *data = (struct client_socket_data*)parser->data;

	printf("URL requested was: %.*s\n", (int)data->url->size, data->url->str);
	
	if(data->url->size >= strlen(announce_base_url)) {
		if(strncmp(data->url->str, announce_base_url, strlen(announce_base_url)) == 0) {
			announce(data);
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
	struct client_socket_data *data = (struct client_socket_data*)parser->data;
	dynamic_string_append(data->url, at, length); 
	return 0;
}


static void read_callback(struct ev_loop *loop, ev_io *watcher, int revents) {
	static char buffer[READSIZE];
	ssize_t read_length;
	struct client_socket_data *data = (struct client_socket_data*)watcher->data;
	
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
	
	struct client_socket_data *data = malloc(sizeof(struct client_socket_data));
	data->loop = loop;
	data->watcher = (ev_io*)malloc(sizeof(ev_io));
	data->sock = client_sock;
	data->url = dynamic_string_init();
	data->shouldfree = 0;

	bzero((void *)&data->parser_settings, sizeof(data->parser_settings));
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
		printf("SIGINT caught.\n");
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
	addr.sin_port = htons(8081);
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
	redis = redisAsyncConnect("127.0.0.1", REDIS_PORT);
	if (redis->err) {
		printf("Failed to connect to redis-server: %s\n", redis->errstr);
		redisAsyncFree(redis);
		return 1;
	}
	redisLibevAttach(EV_DEFAULT_ redis);
	redisAsyncSetConnectCallback(redis,redis_connect_callback);
	redisAsyncSetDisconnectCallback(redis,redis_disconnect_callback);

	struct ev_loop *loop = ev_default_loop(0);
	ev_io *accept_watcher = (ev_io*)malloc(sizeof(ev_io));
	ev_io_init(accept_watcher, accept_callback, sock, EV_READ);

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
