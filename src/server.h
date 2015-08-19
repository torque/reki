#pragma once

typedef struct _httpParserInfo httpParserInfo;

int createServer( uv_loop_t *loop, uv_tcp_t *server );
