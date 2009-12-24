/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_upstream_drizzle.h"
#include "ngx_http_drizzle_processor.h"

enum {
    ngx_http_drizzle_default_port = 3306
};

static ngx_int_t ngx_http_upstream_drizzle_init(ngx_conf_t *cf,
        ngx_http_upstream_srv_conf_t *uscf);

static ngx_int_t ngx_http_upstream_drizzle_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *uscf);

static ngx_int_t ngx_http_upstream_drizzle_get_peer(ngx_peer_connection_t *pc,
        void *data);

static void ngx_http_upstream_drizzle_free_peer(ngx_peer_connection_t *pc,
        void *data, ngx_uint_t state);

/* just a work-around to override the default u->output_filter */
static ngx_int_t ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in);


void *
ngx_http_upstream_drizzle_create_conf(ngx_conf_t *cf)
{
    ngx_http_upstream_drizzle_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
                       sizeof(ngx_http_upstream_drizzle_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->peers   = NULL;
     *     conf->current = 0;
     *     conf->servers = NULL;
     *     conf->drizzle = (drizzle_st)0;
     */

    return conf;
}


/* mostly based on ngx_http_upstream_server in
 * ngx_http_upstream.c of nginx 0.8.30.
 * Copyright (C) Igor Sysoev */
char *
ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    ngx_http_upstream_drizzle_srv_conf_t        *dscf = conf;
    ngx_http_upstream_drizzle_server_t          *ds;
    ngx_str_t                                   *value;
    ngx_url_t                                    u;
    ngx_uint_t                                   i;
    ngx_http_upstream_srv_conf_t                *uscf;

    if (dscf->servers == NULL) {
        dscf->servers = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_http_upstream_drizzle_server_t));
        if (dscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    ds = ngx_array_push(dscf->servers);
    if (ds == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(ds, sizeof(ngx_http_upstream_drizzle_server_t));

    value = cf->args->elts;

    /* parse the first name:port argument */

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.default_port = 80;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "%s in drizzle upstream \"%V\"", u.err, &u.url);
        }

        return NGX_CONF_ERROR;
    }

    ds->addrs  = u.addrs;
    ds->naddrs = u.naddrs;
    ds->port   = u.port;

    /* parse various options */

    for (i = 2; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "dbname=", sizeof("dbname=") - 1)
                == 0)
        {
            ds->dbname.len = value[i].len - (sizeof("dbname=") - 1);
            ds->dbname.data = &value[i].data[sizeof("dbname=") - 1];

            continue;
        }

        if (ngx_strncmp(value[i].data, "user=", sizeof("user=") - 1)
                == 0)
        {
            ds->user.len = value[i].len - (sizeof("user=") - 1);
            ds->user.data = &value[i].data[sizeof("user=") - 1];

            continue;
        }

        if (ngx_strncmp(value[i].data, "password=", sizeof("password=") - 1)
                == 0)
        {
            ds->password.len = value[i].len - (sizeof("password=") - 1);
            ds->password.data = &value[i].data[sizeof("password=") - 1];

            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);

        return NGX_CONF_ERROR;
    }

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    uscf->peer.init_upstream = ngx_http_upstream_drizzle_init;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_upstream_drizzle_init(ngx_conf_t *cf,
        ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_uint_t                               i, j, n;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_drizzle_server_t      *server;
    ngx_http_upstream_drizzle_peers_t       *peers;

    uscf->peer.init = ngx_http_upstream_drizzle_init_peer;

    dscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_drizzle_module);

    if (dscf->servers) {
        server = uscf->servers->elts;

        n = 0;

        for (i = 0; i < uscf->servers->nelts; i++) {
            n += server[i].naddrs;
        }

        peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_drizzle_peers_t)
                + sizeof(ngx_http_upstream_drizzle_peer_t) * (n - 1));

        if (peers == NULL) {
            return NGX_ERROR;
        }

        peers->single = (n == 1);
        peers->number = n;
        peers->name = &uscf->host;

        n = 0;

        for (i = 0; i < uscf->servers->nelts; i++) {
            for (j = 0; j < server[i].naddrs; j++) {
                peers->peer[n].sockaddr = server[i].addrs[j].sockaddr;
                peers->peer[n].socklen = server[i].addrs[j].socklen;
                peers->peer[n].name = server[i].addrs[j].name;
                peers->peer[n].port = server[i].port;
                peers->peer[n].user = server[i].user;
                peers->peer[n].password = server[i].password;
                peers->peer[n].dbname = server[i].dbname;

                n++;
            }
        }

        dscf->peers = peers;

        return NGX_OK;
    }

    /* XXX an upstream implicitly defined by drizzle_pass, etc.,
     * is not allowed for now */

    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                  "no drizzle_server defined in upstream \"%V\" in %s:%ui",
                  &uscf->host, uscf->file_name, uscf->line);

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_drizzle_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_http_upstream_drizzle_peer_data_t   *dp;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_t                     *u;
    ngx_http_drizzle_loc_conf_t             *dlcf;
    ngx_str_t                                dbname;
    ngx_str_t                                query;

    dp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_drizzle_peer_data_t));
    if (dp == NULL) {
        return NGX_ERROR;
    }

    u = r->upstream;

    dscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_drizzle_module);

    dp->conf     = dscf;
    dp->upstream = u;
    dp->request  = r;

    dp->query.len  = 0;
    dp->dbname.len = 0;

    /* to force ngx_output_chain not to use ngx_chain_writer */

    u->output.output_filter = (ngx_event_pipe_output_filter_pt)
                                ngx_http_drizzle_output_filter;
    u->output.filter_ctx = NULL;
    u->output.in   = NULL;
    u->output.busy = NULL;

    u->peer.data = dp;
    u->peer.get = ngx_http_upstream_drizzle_get_peer;
    u->peer.free = ngx_http_upstream_drizzle_free_peer;

    dlcf = ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    /* prepare dbname */

    if (dlcf->dbname) {
        /* check if dbname requires overriding at request time */
        if (ngx_http_complex_value(r, dlcf->dbname, &dbname) != NGX_OK) {
            return NGX_ERROR;
        }

        if (dbname.len) {
            dp->dbname = dbname;
        }
    }

    /* prepare SQL query */

    if (dlcf->query == NULL) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                       "empty \"query\" in drizzle upstream \"%V\"",
                       dscf->peers->name);

        goto empty_query;
    }

    if (ngx_http_complex_value(r, dlcf->query, &query) != NGX_OK) {
        return NGX_ERROR;
    }

    if (query.len == 0) {
        goto empty_query;
    }

    dp->query = query;

    return NGX_OK;

empty_query:

    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                   "empty \"query\" in drizzle upstream \"%V\"",
                   dscf->peers->name);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_upstream_drizzle_get_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_drizzle_peer_data_t   *dp = data;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_drizzle_peers_t       *peers;
    ngx_http_upstream_drizzle_peer_t        *peer;
    ngx_http_request_t                      *r;

    dscf = dp->conf;

    peers = dscf->peers;

    if (dscf->current >= peers->number) {
        dscf->current = 0;
    }

    peer = &peers->peer[dscf->current++];

    r = dp->request;

    /* set up the peer's drizzle connection */

    return NGX_DONE;
}


static void
ngx_http_upstream_drizzle_free_peer(ngx_peer_connection_t *pc,
        void *data, ngx_uint_t state)
{
}


static ngx_int_t
ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in)
{
    return ngx_http_drizzle_process_events(r);
}

