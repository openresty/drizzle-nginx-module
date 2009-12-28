/* Copyright (C) chaoslawful */
/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_handler.h"
#include "ngx_http_upstream_drizzle.h"

/* Forward declaration */

static char * ngx_http_drizzle_set_complex_value_slot(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char * ngx_http_drizzle_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void * ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf);

static char * ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent,
        void *child);


/* config directives for module drizzle */
static ngx_command_t ngx_http_drizzle_cmds[] = {
    {
        ngx_string("drizzle_server"),
        NGX_HTTP_UPS_CONF|NGX_CONF_1MORE,
        ngx_http_upstream_drizzle_server,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL },
    {
        ngx_string("drizzle_query"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
            |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_drizzle_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, query),
        NULL },
    {
        ngx_string("drizzle_dbname"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
            |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_drizzle_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, dbname),
        NULL },
    {
        ngx_string("drizzle_pass"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
            |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_drizzle_pass,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
    { ngx_string("drizzle_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_drizzle_loc_conf_t, upstream.connect_timeout),
      NULL },
    { ngx_string("drizzle_send_query_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_drizzle_loc_conf_t, upstream.send_timeout),
      NULL },
    { ngx_string("drizzle_recv_cols_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_drizzle_loc_conf_t, upstream.send_timeout),
      NULL },
    { ngx_string("drizzle_recv_rows_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_drizzle_loc_conf_t, upstream.send_timeout),
      NULL },

    ngx_null_command
};


/* Nginx HTTP subsystem module hooks */
static ngx_http_module_t ngx_http_drizzle_module_ctx = {
    NULL,    /* preconfiguration */
    NULL,    /* postconfiguration */

    NULL,    /* create_main_conf */
    NULL,    /* merge_main_conf */

    ngx_http_upstream_drizzle_create_srv_conf,
             /* create_srv_conf */
    NULL,    /* merge_srv_conf */

    ngx_http_drizzle_create_loc_conf,    /* create_loc_conf */
    ngx_http_drizzle_merge_loc_conf      /* merge_loc_conf */
};


ngx_module_t ngx_http_drizzle_module = {
    NGX_MODULE_V1,
    &ngx_http_drizzle_module_ctx,       /* module context */
    ngx_http_drizzle_cmds,              /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,    /* init master */
    NULL,    /* init module */
    NULL,    /* init process */
    NULL,    /* init thread */
    NULL,    /* exit thread */
    NULL,    /* exit process */
    NULL,    /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_drizzle_loc_conf_t             *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_drizzle_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;

    conf->recv_cols_timeout = NGX_CONF_UNSET_MSEC;
    conf->recv_rows_timeout = NGX_CONF_UNSET_MSEC;

    /* the hardcoded values */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    /* set by ngx_pcalloc:
     *      conf->dbname = NULL
     *      conf->query  = NULL
     */

    return conf;
}


static char *
ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_drizzle_loc_conf_t *prev = parent;
    ngx_http_drizzle_loc_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->recv_cols_timeout,
                              prev->recv_cols_timeout, 60000);

    ngx_conf_merge_msec_value(conf->recv_rows_timeout,
                              prev->recv_rows_timeout, 60000);

    if (conf->dbname == NULL) {
        conf->dbname = prev->dbname;
    }

    if (conf->query == NULL) {
        conf->query = prev->query;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_drizzle_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    char                             *p = conf;
    ngx_http_complex_value_t        **field;
    ngx_str_t                        *value;
    ngx_http_compile_complex_value_t  ccv;

    field = (ngx_http_complex_value_t **) (p + cmd->offset);

    if (*field) {
        return "is duplicate";
    }

    *field = ngx_pcalloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (*field == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    if (value[1].len == 0) {
        return NGX_OK;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = *field;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_drizzle_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_drizzle_loc_conf_t             *dlcf = conf;
    ngx_http_core_loc_conf_t                *clcf;
    ngx_str_t                               *value;
    ngx_url_t                                u;

    if (dlcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;
    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    dlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);

    if (dlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_drizzle_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    return NGX_CONF_OK;
}

