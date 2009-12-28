/* Copyright (C) chaoslawful */
/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_upstream_drizzle.h"
#include "ngx_http_drizzle_processor.h"

#include <libdrizzle/drizzle_client.h>

ngx_int_t
ngx_http_drizzle_process_events(ngx_http_request_t *r)
{
    ngx_http_upstream_t                         *u;
    ngx_connection_t                            *c;
    ngx_http_upstream_drizzle_peer_data_t       *dp;
    drizzle_con_st                              *dc;
    drizzle_return_t                             ret;

    dd("drizzle process events");

    u = r->upstream;
    c = u->peer.connection;

    dp = u->peer.data;
    dc = &dp->drizzle_con;

    dd("dp state: %d", dp->state);

    /* libdrizzle uses standard poll() event constants
     * and depends on drizzle_con_wait() to set them.
     * we can directly call drizzle_con_wait() here to
     * set those drizzle internal event states, because
     * epoll() and other underlying event mechamism used
     * by the nginx core can play well enough with poll().
     * */
    drizzle_con_wait(dc->drizzle);

#if 0
    switch (dp->state) {
    case state_db_connect:
        rc = ngx_http_upstream_drizzle_connect(r, c, dp, dc);
        if (rc == NGX_AGAIN) {
        }
    }
#endif

    ret = drizzle_con_connect(dc);

    if (ret != DRIZZLE_RETURN_OK && ret != DRIZZLE_RETURN_IO_WAIT) {
       ngx_log_error(NGX_LOG_EMERG, c->log, 0,
                       "failed to connect: %d: %s in drizzle upstream \"%V\"",
                       (int) ret,
                       drizzle_error(dc->drizzle),
                       &u->peer.name);

       return NGX_ERROR;
    }

    dd("ret value: %d", ret);

    if (ret == DRIZZLE_RETURN_OK) {
    }

    /* TODO */

    return NGX_OK;
}

