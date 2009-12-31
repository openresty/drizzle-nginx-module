/* Copyright (C) Maxim Dounin */
/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_keepalive.h"
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

ngx_int_t
ngx_http_drizzle_keepalive_init(ngx_pool_t *pool,
        ngx_http_upstream_drizzle_srv_conf_t *dscf)
{
    ngx_uint_t                              i;
    ngx_http_drizzle_keepalive_cache_t     *cached;

    /* allocate cache items and add to free queue */

    cached = ngx_pcalloc(pool,
                sizeof(ngx_http_drizzle_keepalive_cache_t) * dscf->max_cached);
    if (cached == NULL) {
        return NGX_ERROR;
    }

    ngx_queue_init(&dscf->cache);
    ngx_queue_init(&dscf->free);

    for (i = 0; i < dscf->max_cached; i++) {
        ngx_queue_insert_head(&dscf->free, &cached[i].queue);
        cached[i].srv_conf = dscf;
    }

    return NGX_OK;
}

