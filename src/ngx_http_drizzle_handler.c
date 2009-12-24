/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_handler.h"
#include "ngx_http_drizzle_processor.h"
#include "ngx_http_drizzle_util.h"

/* for read/write event handlers */
static void ngx_http_drizzle_rw_handler(ngx_http_request_t *r,
        ngx_http_upstream_t *u);

static ngx_int_t ngx_http_drizzle_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_drizzle_reinit_request(ngx_http_request_t *r);
static void ngx_http_drizzle_abort_request(ngx_http_request_t *r);
static void ngx_http_drizzle_finalize_request(ngx_http_request_t *r,
        ngx_int_t rc);


ngx_int_t
ngx_http_drizzle_handler(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_http_upstream_t            *u;
    ngx_http_drizzle_ctx_t         *ctx;
    ngx_http_drizzle_loc_conf_t    *mlcf;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    u->schema.len = sizeof("drizzle://") - 1;
    u->schema.data = (u_char *) "drizzle://";

    u->output.tag = (ngx_buf_tag_t) &ngx_http_drizzle_module;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    u->conf = &mlcf->upstream;

    u->create_request = ngx_http_drizzle_create_request;
    u->reinit_request = ngx_http_drizzle_reinit_request;
    u->process_header = NULL;
    u->abort_request = ngx_http_drizzle_abort_request;
    u->finalize_request = ngx_http_drizzle_finalize_request;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_drizzle_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->state = state_db_init;

    ngx_http_set_ctx(r, ctx, ngx_http_drizzle_module);

    /* we bypass the upstream input filter mechanism in
     * ngx_http_upstream_process_headers */

    u->input_filter_init = NULL;
    u->input_filter = NULL;
    u->input_filter_ctx = NULL;

#if defined(nginx_version) && nginx_version >= 8011
    r->main->count++;
#endif

    ngx_http_upstream_init(r);

    /* override the read/write event handler to our own */
    u->write_event_handler = ngx_http_drizzle_rw_handler;
    u->read_event_handler  = ngx_http_drizzle_rw_handler;

    return NGX_DONE;
}


static void
ngx_http_drizzle_rw_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_connection_t            *c;

    c = u->peer.connection;

    if (c->write->timedout) {
        /* XXX we can't call ngx_http_upstream_next because it's
         * declared static. sigh. */
        ngx_http_upstream_drizzle_finalize_request(r, u,
                NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

    if (c->read->timedout) {
        /* XXX we can't call ngx_http_upstream_next because it's
         * declared static. sigh. */
        ngx_http_upstream_drizzle_finalize_request(r, u,
                NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

    ngx_http_drizzle_process_events(r);
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
    u->write_event_handler = ngx_http_drizzle_rw_handler;
    u->read_event_handler  = ngx_http_drizzle_rw_handler;

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

