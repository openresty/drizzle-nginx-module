/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_upstream_drizzle.h"
#include "ngx_http_drizzle_processor.h"

enum {
    ngx_http_drizzle_default_port = 3306
};

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


char *
ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    ngx_http_upstream_drizzle_srv_conf_t        *dscf = conf;
    ngx_http_upstream_drizzle_server_t          *ds;
    ngx_str_t                                   *value;
    ngx_url_t                                    u;
    ngx_uint_t                                   i;

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

        goto invalid;
    }

    return NGX_CONF_OK;

invalid:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid parameter \"%V\"", &value[i]);

    return NGX_CONF_ERROR;
}


static ngx_int_t
ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in)
{
    return ngx_http_drizzle_process_events(r);
}

