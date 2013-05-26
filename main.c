#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ev.h>
#include <strings.h>

#define PORT 8081

static void read_callback(struct ev_loop *loop, ev_io *watcher, int revents) {
	char stuff[100000];
	ssize_t read_length;
	
	read_length = recv(watcher->fd, stuff, 100000, 0);
	if(read_length == -1) {
		perror("Could not read");
		return;
	}
	
	if(read_length == 0) {
		ev_io_stop(loop, watcher);
		free(watcher);
		printf("Connection closed\n");
		return;
	}

	printf("Read %d bytes\n", read_length);
	printf("%.*s", read_length, stuff);
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
	printf("Connection accepted from %s\n", address_str);

	ev_io *read_watcher = (ev_io*)malloc(sizeof(ev_io));
	ev_io_init(read_watcher, read_callback, client_sock, EV_READ);
	ev_io_start(loop, read_watcher);
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

	free(accept_watcher);
	close(sock);

	return 0;
}
