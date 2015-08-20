#pragma once

typedef struct _HttpParserInfo HttpParserInfo;

int createServer( uv_loop_t *loop, uv_tcp_t *server );
