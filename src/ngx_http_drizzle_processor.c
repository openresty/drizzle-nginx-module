/* Copyright (C) chaoslawful */
/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_processor.h"
#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_util.h"
#include "ngx_http_drizzle_output.h"
#include "ngx_http_upstream_drizzle.h"

#include <libdrizzle/drizzle_client.h>

static ngx_int_t ngx_http_upstream_drizzle_connect(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc);

static ngx_int_t ngx_http_upstream_drizzle_send_query(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc);

static ngx_int_t ngx_http_upstream_drizzle_recv_cols(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc);

static ngx_int_t ngx_http_upstream_drizzle_recv_rows(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc);


ngx_int_t
ngx_http_drizzle_process_events(ngx_http_request_t *r)
{
    ngx_http_upstream_t                         *u;
    ngx_connection_t                            *c;
    ngx_http_upstream_drizzle_peer_data_t       *dp;
    drizzle_con_st                              *dc;
    ngx_int_t                                    rc;

    dd("drizzle process events");

    u = r->upstream;
    c = u->peer.connection;

    dp = u->peer.data;
    dc = &dp->drizzle_con;

    /* libdrizzle uses standard poll() event constants
     * and depends on drizzle_con_wait() to set them.
     * we can directly call drizzle_con_wait() here to
     * set those drizzle internal event states, because
     * epoll() and other underlying event mechamism used
     * by the nginx core can play well enough with poll().
     * */
    drizzle_con_wait(dc->drizzle);

    switch (dp->state) {
    case state_db_connect:
        rc = ngx_http_upstream_drizzle_connect(r, c, dp, dc);
        break;

    case state_db_send_query:
        rc = ngx_http_upstream_drizzle_send_query(r, c, dp, dc);
        break;

    case state_db_recv_cols:
        rc = ngx_http_upstream_drizzle_recv_cols(r, c, dp, dc);
        break;

    case state_db_recv_rows:
        rc = ngx_http_upstream_drizzle_recv_rows(r, c, dp, dc);
        break;

    default:
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                       "drizzle: unknown state: %d", (int) dp->state);
        return NGX_ERROR;
    }

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_upstream_drizzle_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);

        return NGX_ERROR;
    }

    return rc;
}


static ngx_int_t
ngx_http_upstream_drizzle_connect(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc)
{
    ngx_http_upstream_t         *u;
    drizzle_return_t             ret;

    dd("drizzle connect");

    u = r->upstream;

    ret = drizzle_con_connect(dc);

    if (ret == DRIZZLE_RETURN_IO_WAIT) {
        return NGX_AGAIN;
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    if (ret != DRIZZLE_RETURN_OK) {
       ngx_log_error(NGX_LOG_ERR, c->log, 0,
                       "drizzle: failed to connect: %d: %s in upstream \"%V\"",
                       (int) ret,
                       drizzle_error(dc->drizzle),
                       &u->peer.name);

       return NGX_ERROR;
    }

    /* ret == DRIZZLE_RETURN_OK */

    return ngx_http_upstream_drizzle_send_query(r, c, dp, dc);
}


static ngx_int_t
ngx_http_upstream_drizzle_send_query(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc)
{
    ngx_http_upstream_t         *u = r->upstream;
    drizzle_return_t             ret;
    ngx_int_t                    rc;

    dd("drizzle send query");

    (void) drizzle_query(dc, &dp->drizzle_res, (const char *) dp->query.data,
            dp->query.len, &ret);

    if (ret == DRIZZLE_RETURN_IO_WAIT) {

        if (dp->state != state_db_send_query) {
            dp->state = state_db_send_query;

            if (c->write->timer_set) {
                ngx_del_timer(c->write);
            }

            ngx_add_timer(c->write, u->conf->send_timeout);

        }

        return NGX_AGAIN;
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    if (ret != DRIZZLE_RETURN_OK) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                       "drizzle: failed to send query: %d: %s"
                       " in upstream \"%V\"",
                       (int) ret,
                       drizzle_error(dc->drizzle),
                       &u->peer.name);

        return NGX_ERROR;
    }

    /* ret == DRIZZLE_RETURN_OK */

    dd_drizzle_result(&dp->drizzle_res);

    rc = ngx_http_drizzle_output_result_header(r, &dp->drizzle_res);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return ngx_http_upstream_drizzle_recv_cols(r, c, dp, dc);
}


static ngx_int_t
ngx_http_upstream_drizzle_recv_cols(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc)
{
    ngx_http_upstream_t             *u = r->upstream;
    drizzle_column_st               *col;
    ngx_int_t                        rc;
    drizzle_return_t                 ret;

    dd("drizzle recv cols");

    for (;;) {
        col = drizzle_column_read(&dp->drizzle_res, &dp->drizzle_col, &ret);

        if (ret == DRIZZLE_RETURN_IO_WAIT) {

            if (dp->state != state_db_recv_cols) {
                dp->state = state_db_recv_cols;

                if (c->read->timer_set) {
                    ngx_del_timer(c->read);
                }

                ngx_add_timer(c->read, dp->loc_conf->recv_cols_timeout);
            }

            return NGX_AGAIN;
        }

        if (ret != DRIZZLE_RETURN_OK) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                           "drizzle: failed to recv cols: %d: %s"
                           " in upstream \"%V\"",
                           (int) ret,
                           drizzle_error(dc->drizzle),
                           &u->peer.name);

            return NGX_ERROR;
        }

        /* ret == DRIZZLE_RETURN_OK */

        rc = ngx_http_drizzle_output_col(r, col);

        if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        if (col == NULL) { /* after the last column */
            return ngx_http_upstream_drizzle_recv_rows(r, c, dp, dc);
        }

        dd_drizzle_column(col);

        dp->state = state_db_recv_cols;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_drizzle_recv_rows(ngx_http_request_t *r,
        ngx_connection_t *c, ngx_http_upstream_drizzle_peer_data_t *dp,
        drizzle_con_st *dc)
{
    /* TODO */
    return NGX_OK;
}

