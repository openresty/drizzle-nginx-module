#ifndef NGX_HTTP_DRIZZLE_UTIL_H
#define NGX_HTTP_DRIZZLE_UTIL_H

void
ngx_http_upstream_drizzle_finalize_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_int_t rc);

#endif /* NGX_HTTP_DRIZZLE_UTIL_H */

