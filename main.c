#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ev.h>
#include <strings.h>
#include "http-parser/http_parser.h"

#define PORT 8081
#define READSIZE 1024
#define DYNAMICSTRINGSIZE 1024

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

static int parser_message_complete_callback(http_parser *parser) {
	struct client_socket_data *data = (struct client_socket_data*)parser->data;

	//printf("URL requested was: %.*s\n", data->url->size, data->url->str);

	const char *reply = "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi";
	send(data->sock, reply, strlen(reply), 0);

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
		printf("SIGINT catched\n");
		ev_unloop(EV_A_ EVUNLOOP_ALL);
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
