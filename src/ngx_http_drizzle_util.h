#ifndef NGX_HTTP_DRIZZLE_UTIL_H
#define NGX_HTTP_DRIZZLE_UTIL_H

#include "ddebug.h"

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_http.h>


void ngx_http_upstream_dbd_init(ngx_http_request_t *r);

void ngx_http_upstream_dbd_init_request(ngx_http_request_t *r);

ngx_int_t ngx_http_drizzle_set_header(ngx_http_request_t *r, ngx_str_t *key,
        ngx_str_t *value);

void ngx_http_upstream_drizzle_finalize_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_int_t rc);

void ngx_http_upstream_drizzle_next(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_int_t rc);

ngx_int_t ngx_http_upstream_drizzle_test_connect(ngx_connection_t *c);


#define ngx_http_drizzle_nelems(x) (sizeof(x) / sizeof(x[0]))

#ifndef ngx_str3cmp

#  define ngx_str3cmp(m, c0, c1, c2)                                       \
    m[0] == c0 && m[1] == c1 && m[2] == c2

#endif /* ngx_str3cmp */


#ifndef ngx_str4cmp

#  if (NGX_HAVE_LITTLE_ENDIAN && NGX_HAVE_NONALIGNED)

#    define ngx_str4cmp(m, c0, c1, c2, c3)                                        \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)

#  else

#    define ngx_str4cmp(m, c0, c1, c2, c3)                                        \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3

#  endif

#endif /* ngx_str4cmp */


#ifndef ngx_str5cmp

#  if (NGX_HAVE_LITTLE_ENDIAN && NGX_HAVE_NONALIGNED)

#    define ngx_str5cmp(m, c0, c1, c2, c3, c4)                                    \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)             \
        && m[4] == c4

#  else

#    define ngx_str5cmp(m, c0, c1, c2, c3, c4)                                    \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3 && m[4] == c4

#  endif

#endif /* ngx_str5cmp */


#ifndef ngx_str6cmp

#  if (NGX_HAVE_LITTLE_ENDIAN && NGX_HAVE_NONALIGNED)

#    define ngx_str6cmp(m, c0, c1, c2, c3, c4, c5)                                \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)             \
        && (((uint32_t *) m)[1] & 0xffff) == ((c5 << 8) | c4)

#  else

#    define ngx_str6cmp(m, c0, c1, c2, c3, c4, c5)                                \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3                      \
        && m[4] == c4 && m[5] == c5

#  endif

#endif /* ngx_str6cmp */


#ifndef ngx_str7cmp

#  define ngx_str7cmp(m, c0, c1, c2, c3, c4, c5, c6)                          \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3                      \
        && m[4] == c4 && m[5] == c5 && m[6] == c6

#endif /* ngx_str7cmp */


#ifndef ngx_str9cmp

#  if (NGX_HAVE_LITTLE_ENDIAN && NGX_HAVE_NONALIGNED)

#    define ngx_str9cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                    \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)             \
        && ((uint32_t *) m)[1] == ((c7 << 24) | (c6 << 16) | (c5 << 8) | c4)  \
        && m[8] == c8

#  else

#    define ngx_str9cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                    \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3                      \
        && m[4] == c4 && m[5] == c5 && m[6] == c6 && m[7] == c7 && m[8] == c8

#  endif


#endif /* ngx_str9cmp */

#endif /* NGX_HTTP_DRIZZLE_UTIL_H */

