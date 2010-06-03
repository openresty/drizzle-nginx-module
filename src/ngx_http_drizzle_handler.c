/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_handler.h"
#include "ngx_http_drizzle_processor.h"
#include "ngx_http_drizzle_util.h"
#include "ngx_http_upstream_drizzle.h"

/* for read/write event handlers */


static ngx_int_t ngx_http_drizzle_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_drizzle_reinit_request(ngx_http_request_t *r);
static void ngx_http_drizzle_abort_request(ngx_http_request_t *r);
static void ngx_http_drizzle_finalize_request(ngx_http_request_t *r,
        ngx_int_t rc);

static ngx_int_t ngx_http_drizzle_process_header(ngx_http_request_t *r);

static ngx_int_t ngx_http_drizzle_input_filter_init(void *data);

static ngx_int_t ngx_http_drizzle_input_filter(void *data, ssize_t bytes);


ngx_int_t
ngx_http_drizzle_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t            *u;
    ngx_http_drizzle_loc_conf_t    *dlcf;
    ngx_str_t                       target;
    ngx_url_t                       url;
    ngx_connection_t               *c;

    if (r->subrequest_in_memory) {
        /* TODO: add support for subrequest in memory by
         * emitting output into u->buffer instead */

        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "ngx_http_drizzle_module does not support "
                      "subrequest in memory");

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

#if defined(nginx_version) && \
    ((nginx_version >= 7063 && nginx_version < 8000) \
     || nginx_version >= 8007)

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

#else /* 0.7.x < 0.7.63, 0.8.x < 0.8.7 */

    u = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_t));
    if (u == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u->peer.log = r->connection->log;
    u->peer.log_error = NGX_ERROR_ERR;
#  if (NGX_THREADS)
    u->peer.lock = &r->connection->lock;
#  endif

    r->upstream = u;

#endif

    dlcf = ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    if (dlcf->complex_target) {
        /* variables used in the drizzle_pass directive */
        if (ngx_http_complex_value(r, dlcf->complex_target, &target)
                != NGX_OK)
        {
            dd("failed to compile");
            return NGX_ERROR;
        }

        if (target.len == 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "drizzle: handler: empty \"drizzle_pass\" target");
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        url.host = target;
        url.port = 0;
        url.no_resolve = 1;

        dlcf->upstream.upstream = ngx_http_upstream_drizzle_add(r, &url);

        if (dlcf->upstream.upstream == NULL) {
            //dd("upstream \"%.*s\" not found", target.len, target.data);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    u->schema.len = sizeof("drizzle://") - 1;
    u->schema.data = (u_char *) "drizzle://";

    u->output.tag = (ngx_buf_tag_t) &ngx_http_drizzle_module;

    dd("drizzle tag: %p", (void *) u->output.tag);

    u->conf = &dlcf->upstream;

    u->create_request = ngx_http_drizzle_create_request;
    u->reinit_request = ngx_http_drizzle_reinit_request;
    u->process_header = ngx_http_drizzle_process_header;
    u->abort_request = ngx_http_drizzle_abort_request;
    u->finalize_request = ngx_http_drizzle_finalize_request;

    /* we bypass the upstream input filter mechanism in
     * ngx_http_upstream_process_headers */

    u->input_filter_init = ngx_http_drizzle_input_filter_init;
    u->input_filter = ngx_http_drizzle_input_filter;
    u->input_filter_ctx = NULL;

#if defined(nginx_version) && nginx_version >= 8011
    r->main->count++;
#endif

    ngx_http_upstream_dbd_init(r);

    /* override the read/write event handler to our own */
    u->write_event_handler = ngx_http_drizzle_wev_handler;
    u->read_event_handler  = ngx_http_drizzle_rev_handler;

    /* a bit hack-ish way to return 503 Service Unavailable (clean-up part) */
    if ((u->peer.connection) && (u->peer.connection->fd == 0)) {
        c = u->peer.connection;
        u->peer.connection = NULL;

        if (c->write->timer_set) {
            ngx_del_timer(c->write);
        }

        ngx_free_connection(c);

        ngx_http_upstream_drizzle_finalize_request(r, u,
            NGX_HTTP_SERVICE_UNAVAILABLE);
    }

    return NGX_DONE;
}


void
ngx_http_drizzle_wev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_connection_t            *c;

    dd("drizzle wev handler");

    /* just to ensure u->reinit_request always gets called for
     * upstream_next */
    u->request_sent = 1;

    c = u->peer.connection;

    if (c->write->timedout) {
        dd("drizzle connection write timeout");

        ngx_http_upstream_drizzle_next(r, u,
                NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

    if (ngx_http_upstream_drizzle_test_connect(c) != NGX_OK) {
        dd("drizzle connection is broken");

        ngx_http_upstream_drizzle_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }

    ngx_http_drizzle_set_libdrizzle_ready(r);

    (void) ngx_http_drizzle_process_events(r);
}


void
ngx_http_drizzle_rev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_connection_t            *c;

    dd("drizzle rev handler");

    /* just to ensure u->reinit_request always gets called for
     * upstream_next */
    u->request_sent = 1;

    c = u->peer.connection;

    if (c->read->timedout) {
        dd("drizzle connection read timeout");

        ngx_http_upstream_drizzle_next(r, u,
                NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

    if (ngx_http_upstream_drizzle_test_connect(c) != NGX_OK) {
        dd("drizzle connection is broken");

        ngx_http_upstream_drizzle_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }

    ngx_http_drizzle_set_libdrizzle_ready(r);

    (void) ngx_http_drizzle_process_events(r);
}


static ngx_int_t
ngx_http_drizzle_create_request(ngx_http_request_t *r)
{
    r->upstream->request_bufs = NULL;

    return NGX_OK;
}


static ngx_int_t
ngx_http_drizzle_reinit_request(ngx_http_request_t *r)
{
    ngx_http_upstream_t         *u;

    u = r->upstream;

    /* override the read/write event handler to our own */
    u->write_event_handler = ngx_http_drizzle_wev_handler;
    u->read_event_handler  = ngx_http_drizzle_rev_handler;

    return NGX_OK;
}


static void
ngx_http_drizzle_abort_request(ngx_http_request_t *r)
{
}


static void
ngx_http_drizzle_finalize_request(ngx_http_request_t *r,
        ngx_int_t rc)
{
}


static ngx_int_t
ngx_http_drizzle_process_header(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
           "ngx_http_drizzle_process_header should not be called"
           " by the upstream");

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_drizzle_input_filter_init(void *data)
{
    ngx_http_request_t          *r = data;

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
           "ngx_http_drizzle_input_filter_init should not be called"
           " by the upstream");

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_drizzle_input_filter(void *data, ssize_t bytes)
{
    ngx_http_request_t          *r = data;

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
           "ngx_http_drizzle_input_filter should not be called"
           " by the upstream");

    return NGX_ERROR;
}


void
ngx_http_drizzle_set_libdrizzle_ready(ngx_http_request_t *r)
{
    ngx_http_upstream_drizzle_peer_data_t       *dp;
    drizzle_con_st                              *dc;

    dp = r->upstream->peer.data;

    dc = dp->drizzle_con;

    /* libdrizzle uses standard poll() event constants
     * and depends on drizzle_con_wait() to set them.
     * we can directly call drizzle_con_wait() here to
     * set those drizzle internal event states, because
     * epoll() and other underlying event mechamism used
     * by the nginx core can play well enough with poll().
     * */

    (void) drizzle_con_wait(dc->drizzle);
}

