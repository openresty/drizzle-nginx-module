#ifndef NGX_HTTP_UPSTREAM_DRIZZLE_H
#define NGX_HTTP_UPSTREAM_DRIZZLE_H

#include <ngx_core.h>
#include <ngx_http.h>

char * ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

void* ngx_http_upstream_drizzle_create_srv_conf(ngx_conf_t *cf);

#endif /* NGX_HTTP_UPSTREAM_DRIZZLE_H */

