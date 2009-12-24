#ifndef NGX_HTTP_UPSTREAM_DRIZZLE_H
#define NGX_HTTP_UPSTREAM_DRIZZLE_H

#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_addr_t                      *addrs;
    ngx_uint_t                       naddrs;
    in_port_t                        port;
    ngx_str_t                        user;
    ngx_str_t                        password;
    ngx_str_t                        dbname;

} ngx_http_upstream_drizzle_server_t;


typedef struct {
    struct sockaddr                *sockaddr;
    socklen_t                       socklen;
    in_port_t                       port;
    ngx_str_t                       user;
    ngx_str_t                       password;
    ngx_str_t                       dbname;
    ngx_str_t                       name;
} ngx_http_upstream_drizzle_peer_t;


typedef struct {
    ngx_uint_t                          single;
    ngx_uint_t                          number;
    ngx_str_t                          *name;

    ngx_http_upstream_drizzle_peer_t    peer[1];

} ngx_http_upstream_drizzle_peers_t;


typedef struct {
    ngx_http_upstream_drizzle_peers_t   *peers;

    /* TODO: we might need "tried" from round robin peer data */
    ngx_uint_t                           current;

    /* of ngx_http_upstream_drizzle_server_t */
    ngx_array_t                         *servers;

    drizzle_st                           drizzle;

} ngx_http_upstream_drizzle_srv_conf_t;


typedef struct {
    ngx_http_upstream_drizzle_srv_conf_t  *conf;

    ngx_http_upstream_t                   *upstream;
    ngx_http_request_t                    *request;

} ngx_http_upstream_drizzle_peer_data_t;


char * ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

void * ngx_http_upstream_drizzle_create_conf(ngx_conf_t *cf);

#endif /* NGX_HTTP_UPSTREAM_DRIZZLE_H */

