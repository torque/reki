#ifndef common_h
#define common_h

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libev.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
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

#ifdef PRODUCTION
	#define PORT 9001
	#define SITE_PORT 9000
	#define DATABASE 0
#else
	#define PORT 3001
	#define SITE_PORT 3000
	#define DATABASE 1
#endif

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

redisAsyncContext *redis; // abuse scope for ease-of-use

void free_client_socket_data(client_socket_data *data);
int intlength(int input);
long long read_int(char *str, int str_size, const int base);
int parse_info_hash(char *output, int output_length, char *input, int input_length);
int parse_peer_id(char *output, char *input, int input_length);
int hex_to_string(char *output, char *input);
void simple_error(client_socket_data *data, char *message);

#endif
