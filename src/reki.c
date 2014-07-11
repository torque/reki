#include <uv.h> // libuv
#include <hiredis/hiredis.h> // hiredis
#include <hiredis/async.h>
#include <hiredis/adapters/libuv.h>
#include <netinet/in.h> // struct sockaddr
#include <signal.h>     // SIGINT
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "dbg.h"
#include "dynamic_string.h"
#include "../http-parser/http_parser.h"

static void redisConnectCb( const redisAsyncContext *redis, int status ) {
	if ( status != REDIS_OK ) {
		dbg_info( "Error: %s", redis->errstr );
		exit( 1 );
	}
	dbg_info( "Connected to redis." );
}

static void redisDisconnectCb( const redisAsyncContext *redis, int status ) {
	if ( status != REDIS_OK ) {
		dbg_info( "Error: %s", redis->errstr );
		return;
	}
	dbg_info( "Disconnected from redis." );
}

// Do this unless evidence occurs that it is a horrible idea.
static void uvBufferAllocStaticCb( uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf ) {
	static char base[1024];
	buf->base = base;
	buf->len = sizeof base;
}

static void replyToClientCb( uv_write_t* reply, int status ) {
	dbg_info( "replyToClient" );
	if ( status < 0 ) {
		dbg_info( "Write error: %s", uv_err_name( status ) );
	}
	clientInfo *client = reply->data;
	free( reply );
	dynamic_string_free( client->reqUrl );
	free( client->announce );
	free( client->settings );
	// DONT DO THIS ITS BAD
	// free( client->handle );
	free( client );
}

static int httpUrlCb( http_parser *parser, const char *at, size_t length ) {
	clientInfo *client = parser->data;
	dynamic_string_append( client->reqUrl, at, length );
	return 0;
}

static int httpHeaderFieldCb( http_parser *parser, const char *at, size_t length ) {
	if ( length > 0 && strncmp( at, "X-Real-IP", length ) == 0 ) {
		dbg_info( "%.*s", (int)length, at );
		clientInfo *client = parser->data;
		client->lastHeaderFieldWasRealIP = true;
	}
	return 0;
}

static int httpHeaderValueCb( http_parser *parser, const char *at, size_t length ) {
	clientInfo *client = parser->data;
	if ( client->lastHeaderFieldWasRealIP ) {
		// dbg_info( "%.*s", (int)length, at );
		client->lastHeaderFieldWasRealIP = false;
		// *at shouldn't get cleaned up because it points to the manually
		// allocated buf->base from clientRequestCb.
		client->announce->peerIp = (char*)at;
		// Need a reasonable way to detect ipv6 vs ipv4 here. The best I can
		// think of is checking for the existence of : in the ip address.
		int i = 0;
		for ( ; i < length; ++i ) {
			if ( at[i] == ':' ) {
				break;
			}
		}
		client->announce->ipType = i == length ? AF_INET : AF_INET6;
	}
	return 0;
}

static int httpHeadersCompleteCb( http_parser *parser ) {
	dbg_info( "httpHeadersComplete" );
	clientInfo *client = parser->data;
	uv_read_stop( (uv_stream_t*)client->handle );

	uv_write_t *reply = calloc( 1, sizeof *reply );
	reply->data = (void*)client;
	uv_buf_t message = uv_buf_init( "HTTP/1.0 200 OK\r\nContent-Type: text/text\r\nConnection: close\r\nContent-Length: 2\r\n\r\nHi", 94 );

	http_parser_parse_url( client->reqUrl->str, client->reqUrl->size, 0, &client->parsedUrl );

	if ( client->parsedUrl.field_set & (1 << UF_PATH) ) {
		dbg_info( "Path: %.*s", client->parsedUrl.field_data[UF_PATH].len, client->reqUrl->str + client->parsedUrl.field_data[UF_PATH].off );
	}

	if ( client->parsedUrl.field_set & (1 << UF_QUERY) ) {
		dbg_info( "Query: %.*s", client->parsedUrl.field_data[UF_QUERY].len, client->reqUrl->str + client->parsedUrl.field_data[UF_QUERY].off );
	}

	uv_write( reply, (uv_stream_t*)client->handle, &message, 1, replyToClientCb);

	return 0;
}

static void clientRequestCb( uv_stream_t *clientConnection, ssize_t nread, const uv_buf_t *buf ) {
	if ( nread < 0 ) {
		dbg_info( "Read error %zd: %s", nread, uv_err_name( nread ) );
		uv_close( (uv_handle_t*)clientConnection, NULL );
		return;
	}

	clientInfo *client = clientConnection->data;
	http_parser_execute( &client->settings->parser, &client->settings->parserSettings, buf->base, nread );
}

// There is probably a way to do this without a separate function for
// ipv4 and ipv6 connections, but i do not know what it is. Maybe
// abusing sockaddr_storage is the way to do it?
static int getClientIp4( clientInfo* client ) {
	struct sockaddr_in peername;
	int ret, namelen = sizeof peername;
	ret = uv_tcp_getpeername( client->handle, (struct sockaddr*)&peername, &namelen );
	if ( ret ) {
		log_err( "Getpeername error: %s", uv_err_name( ret ) );
		return 1;
	}
	char ip[INET_ADDRSTRLEN];
	ret = uv_inet_ntop( AF_INET, &peername.sin_addr, ip, INET_ADDRSTRLEN );
	if ( ret ) {
		log_err( "Ntop error: %s", uv_err_name( ret ) );
		return 1;
	}
	client->announce->ipType = AF_INET;
	client->announce->peerIp = ip;
	dbg_info( "Connection from: %s", ip );
	return 0;
}

static int getClientIp6( clientInfo* client ) {
	struct sockaddr_in6 peername;
	int ret, namelen = sizeof peername;
	ret = uv_tcp_getpeername( client->handle, (struct sockaddr*)&peername, &namelen );
	if ( ret ) {
		log_err( "Getpeername error: %s", uv_err_name( ret ) );
		return 1;
	}
	char ip[INET6_ADDRSTRLEN];
	ret = uv_inet_ntop( AF_INET6, &peername.sin6_addr, ip, INET6_ADDRSTRLEN );
	if ( ret ) {
		log_err( "Ntop error: %s", uv_err_name( ret ) );
		return 1;
	}
	client->announce->ipType = AF_INET6;
	client->announce->peerIp = ip;

	dbg_info( "Connection from: %s", ip );
	return 0;
}

static void newConnectionWithCallback( uv_tcp_t *server, int (*getClientIp)( clientInfo* ) ) {
	dbg_info( "Connection received." );

	uv_tcp_t *clientConnection = calloc( 1, sizeof *clientConnection );
	uv_tcp_init( server->loop, clientConnection );

	if ( uv_accept( (uv_stream_t*)server, (uv_stream_t*)clientConnection ) == 0 ) {

		// allocate all of the structs for the client.
		clientInfo *client = calloc( 1, sizeof *client );
		client->settings = calloc( 1, sizeof *client->settings );
		client->announce = calloc( 1, sizeof *client->announce );
		client->handle = clientConnection;
		client->reqUrl = dynamic_string_init( );
		int ret = getClientIp( client );
		if ( ret ) {
			log_err( "The peer's IP could not be divined." );
			uv_close( (uv_handle_t*)clientConnection, NULL );
			return;
		}

		// set up callbacks
		memset( &client->settings->parserSettings, 0, sizeof client->settings->parserSettings );
		client->settings->parserSettings.on_url = httpUrlCb;
		client->settings->parserSettings.on_header_field = httpHeaderFieldCb;
		client->settings->parserSettings.on_header_value = httpHeaderValueCb;
		client->settings->parserSettings.on_headers_complete = httpHeadersCompleteCb;

		// add references to our data structures
		clientConnection->data = (void*)client;
		client->settings->parser.data = (void*)client;

		http_parser_init( &client->settings->parser, HTTP_REQUEST );
		uv_read_start( (uv_stream_t*)clientConnection, uvBufferAllocStaticCb, clientRequestCb );
	} else {
		uv_close( (uv_handle_t*)clientConnection, NULL );
	}

}

static void newConnectionCb6( uv_stream_t *server, int status ) {
	if ( status < 0 ) {
		return;
	}

	newConnectionWithCallback( (uv_tcp_t*)server, getClientIp6 );
}

static void newConnectionCb4( uv_stream_t *server, int status ) {
	if ( status < 0 ) {
		return;
	}

	newConnectionWithCallback( (uv_tcp_t*)server, getClientIp4 );
}

static void interruptCb( uv_signal_t *interrupt, int signal ) {
	puts( "" );
	log_err( "SIGINT caught. Quitting." );
	redisAsyncDisconnect( interrupt->data );
	uv_stop( interrupt->loop );
}

int main ( int argc, char **argv ) {

	uv_loop_t* loop = uv_default_loop( );

	redisAsyncContext *redis = redisAsyncConnect("127.0.0.1", 6379);
	if ( redis->err ) {
		log_err( "Redis error: %s\n", redis->errstr );
		free( redis );
		return 1;
	}

	uv_tcp_t *serverIp6 = calloc( 1, sizeof *serverIp6 );
	serverIp6->data = (void*)redis;
	uv_tcp_t *serverIp4 = calloc( 1, sizeof *serverIp4 );
	serverIp4->data = (void*)redis;
	uv_tcp_init( loop, serverIp6 );
	uv_tcp_init( loop, serverIp4 );

	int ret6, ret4;
	struct sockaddr_in6 *bindAddressIp6 = calloc( 1, sizeof *bindAddressIp6 );
	struct sockaddr_in *bindAddressIp4 = calloc( 1, sizeof *bindAddressIp4 );

	ret6 = uv_ip6_addr( "::1", 9001, bindAddressIp6 );
	ret4 = uv_ip4_addr( "127.0.0.1", 9001, bindAddressIp4 );
	if ( ret6 || ret4 ) {
		log_err( "IP error: %s", uv_err_name( ret6 ? ret6 : ret4 ) );
		return 1;
	}

	ret6 = uv_tcp_bind( serverIp6, (struct sockaddr*)bindAddressIp6, 0 );
	ret4 = uv_tcp_bind( serverIp4, (struct sockaddr*)bindAddressIp4, 0 );
	if ( ret6 || ret4 ) {
		log_err( "Bind error: %s", uv_err_name( ret6 ? ret6 : ret4 ) );
		return 1;
	}

	ret6 = uv_listen( (uv_stream_t*)serverIp6, 128, newConnectionCb6 );
	ret4 = uv_listen( (uv_stream_t*)serverIp4, 128, newConnectionCb4 );
	if ( ret6 || ret4 ) {
		log_err( "Listen error: %s", uv_err_name( ret6 ? ret6 : ret4 ) );
		return 1;
	}

	dbg_info( "Listening on port 9001." );

	uv_signal_t *interrupt = calloc( 1, sizeof *interrupt );
	interrupt->data = (void*)redis;
	uv_signal_init( loop, interrupt );
	uv_signal_start( interrupt, interruptCb, SIGINT );

	redisLibuvAttach( redis, loop );
	redisAsyncSetConnectCallback( redis, redisConnectCb );
	redisAsyncSetDisconnectCallback( redis, redisDisconnectCb );
	uv_run( loop, UV_RUN_DEFAULT );

	uv_loop_close( loop );
	free( serverIp6 );
	free( serverIp4 );
	free( bindAddressIp6 );
	free( bindAddressIp4 );
	free( interrupt );
	return 0;
}
