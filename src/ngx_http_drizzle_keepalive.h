#ifndef NGX_HTTP_DRIZZLE_KEEPALIVE_H
#define NGX_HTTP_DRIZZLE_KEEPALIVE_H

#include "ngx_http_upstream_drizzle.h"
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

typedef struct {
    ngx_http_upstream_drizzle_srv_conf_t  *srv_conf;

    ngx_queue_t                          queue;

    ngx_connection_t                    *connection;

    socklen_t                            socklen;
    struct sockaddr_storage              sockaddr;
    ngx_str_t                            dbname;
    drizzle_con_st                      *drizzle_con;

} ngx_http_drizzle_keepalive_cache_t;


ngx_int_t ngx_http_drizzle_keepalive_init(ngx_pool_t *pool,
        ngx_http_upstream_drizzle_srv_conf_t *dscf);

#endif /* NGX_HTTP_DRIZZLE_KEEPALIVE_H */

