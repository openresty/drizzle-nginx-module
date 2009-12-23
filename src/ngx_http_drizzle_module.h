#ifndef NGX_HTTP_DRIZZLE_MODULE_H
#define NGX_HTTP_DRIZZLE_MODULE_H

#include <ngx_config.h>
#include <nginx.h>
#include <ngx_http.h>

#include <libdrizzle/drizzle_client.h>

typedef struct {
    /* of ngx_http_upstream_drizzle_server_t */
    ngx_array_t                         *servers;

    drizzle_st                           drizzle;

    /* TODO: we might need "tried" from round robin peer data */
    ngx_uint_t                           current;

    ngx_http_upstream_init_pt            original_init_upstream;
    ngx_http_upstream_init_peer_pt       original_init_peer;

} ngx_http_upstream_drizzle_srv_conf_t;


typedef struct {
    ngx_http_upstream_drizzle_srv_conf_t  *conf;

    ngx_http_upstream_t                   *upstream;
    ngx_http_request_t                    *request;

    void                                  *data;

    ngx_event_get_peer_pt                  original_get_peer;
    ngx_event_free_peer_pt                 original_free_peer;

} ngx_http_upstream_drizzle_peer_data_t;


typedef struct {
    /* drizzle database name */
    ngx_http_complex_value_t            dbname;

    /* SQL query to be executed */
    ngx_http_complex_value_t            query;

} ngx_http_drizzle_loc_conf_t;


/* states for the drizzle client state machine */
typedef enum {
    state_db_init,
    state_db_connect,
    state_db_send_query,
    state_db_recv_fields,
    state_db_recv_rows,
    state_db_fin,
    state_db_err

} ngx_http_drizzle_state_t;


typedef struct {
    ngx_str_t                           query;

    ngx_http_drizzle_state_t            state;

    ngx_connection_t                   *nginx_con;
    drizzle_con_st                      drizzle_con;
    drizzle_result_st                   drizzle_result;

} ngx_http_drizzle_ctx_t;


typedef struct {
    ngx_addr_t                      *addrs;
    ngx_uint_t                       naddrs;

    in_port_t                        port;

    ngx_str_t                        dbname;

/*
    ngx_uint_t                       weight;
    ngx_uint_t                       max_fails;
    time_t                           fail_timeout;

    unsigned                         down:1;
    unsigned                         backup:1;
*/

} ngx_http_upstream_server_t;

#endif /* NGX_HTTP_DRIZZLE_MODULE_H */

