/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_upstream_drizzle.h"

#if 0
/* just a work-around to override the default u->output_filter */
static ngx_int_t ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in);
#endif

void *
ngx_http_upstream_drizzle_create_srv_conf(ngx_conf_t *cf)
{
    return NULL;
}


char *
ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    return NGX_CONF_OK;
}

