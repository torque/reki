#include "reki.h"

static int parser_message_complete_callback(http_parser *parser) {
	client_socket_data *data = (client_socket_data*)parser->data;

	// printf("URL requested was: %.*s\n", (int)data->url->size, data->url->str);
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(data->peer_ip), ip, INET_ADDRSTRLEN);
	dbg_info("%s requested %.*s", ip, (int)data->url->size, data->url->str);

	if (data->url->size >= strlen(announce_base_url)) {
		if(strncmp(data->url->str, announce_base_url, strlen(announce_base_url)) == 0) {
			parse_announce_request(data);
		} else if (strncmp(data->url->str, scrape_base_url, strlen(scrape_base_url)) == 0) {
			parse_scrape_request(data);
		}
	} else {
		const char *reply = "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi";
		send(data->sock, reply, strlen(reply), 0);
		data->shouldfree = 1;
	}

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
		fancy_perror("Could not read");
		return;
	}

	if(read_length == 0) {
		data->shouldfree = 1;
	} else {
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
		fancy_perror("Could not accept connection");
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
		puts("");
		log_err("SIGINT caught.");
		if (!redis->err) {
			redisAsyncDisconnect(redis);
		}
		ev_unloop(EV_A_ EVUNLOOP_ALL);
}

void redis_connect_callback(const redisAsyncContext *redis, int status) {
	if (status != REDIS_OK) {
		log_err("Redis connection error: %s", redis->errstr);
		exit(1);
	}
	log_info("Connected to redis-server on port %d.",REDIS_PORT);
}

void redis_disconnect_callback(const redisAsyncContext *redis, int status) {
	if (status != REDIS_OK) {
		log_err("Redis connection error: %s", redis->errstr);
		return;
	}
	log_info("Disconnected from redis_server.");
}

int main()
{
	int retval;

	log_info("Connecting to redis-server on port %d", REDIS_PORT);
	redis = redisAsyncConnect("127.0.0.1", REDIS_PORT);

	// Create socket
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1) {
		fancy_perror("Could not create socket");
		return 1;
	}

	int socketoption = 1;
	retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &socketoption, sizeof(socketoption));
	if(retval == -1) {
		fancy_perror("Could not set socket options");
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	retval = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if(retval == -1) {
		fancy_perror("Could not bind");
		return 1;
	}

	retval = listen(sock, 1000 /*backlog*/);
	if(retval == -1) {
		fancy_perror("Could not listen");
		return 1;
	}
	log_info("Starting tracker on port %d", PORT);

	struct ev_loop *loop = ev_default_loop(0);
	ev_io *accept_watcher = (ev_io*)malloc(sizeof(ev_io));
	ev_io_init(accept_watcher, accept_callback, sock, EV_READ);

	redisAsyncCommand(redis, NULL, NULL, "SELECT %d", DATABASE);
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
	log_info("Shutting down.");
	free(accept_watcher);
	ev_loop_destroy(loop);
	close(sock);

	return 0;
}
