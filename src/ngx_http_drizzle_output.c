/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_output.h"

static ngx_int_t ngx_http_drizzle_output_chain(ngx_http_request_t *r,
        ngx_chain_t *cl);


ngx_int_t
ngx_http_drizzle_output_result_header(ngx_http_request_t *r,
        drizzle_result_st *res)
{
    ngx_http_upstream_t             *u = r->upstream;
    const char                      *errstr;
    size_t                           size;
    uint16_t                         errstr_len;
    ngx_buf_t                       *b;
    ngx_chain_t                     *cl;

    errstr = drizzle_result_error(res);

    errstr_len = (uint16_t) strlen(errstr);

    size = sizeof(uint8_t)      /* endian type */
         + sizeof(uint32_t)     /* format version */
         + sizeof(uint8_t)      /* result type */

         + sizeof(uint16_t)     /* standard error code */
         + sizeof(uint16_t)     /* driver-specific error code */

         + sizeof(uint16_t)     /* driver-specific errstr len */
         + errstr_len           /* driver-specific errstr data */
         + sizeof(uint64_t)     /* rows affected */
         + sizeof(uint64_t)     /* insert id */
         + sizeof(uint16_t)     /* column count */
         ;

    cl = ngx_chain_get_free_buf(r->pool, &r->upstream->free_bufs);

    if (cl == NULL) {
        return NGX_ERROR;
    }

    b = cl->buf;

    b->tag = u->output.tag;
    b->flush = 1;
    b->memory = 1;
    b->temporary = 1;

    b->start = ngx_palloc(r->pool, size);

    if (b->start == NULL) {
        return NGX_ERROR;
    }

    b->end = b->start + size;
    b->pos = b->last = b->start;

#if NGX_HAVE_LITTLE_ENDIAN
    *b->last++ = 0;
#else /* big endian */
    *b->last++ = 1;
#endif

    /* format version 0.0.1 */

    *(uint32_t *) b->last = 1;
    b->last += sizeof(uint32_t);

    /* result type fixed to 0 */
    *b->last++ = 0;

    /* standard error code
     * FIXME: define the standard error code set and map
     * libdrizzle's to it. */
    *(uint16_t *) b->last = drizzle_result_error_code(res);
    b->last += sizeof(uint16_t);

     /* driver-specific error code */
    *(uint16_t *) b->last = drizzle_result_error_code(res);
    b->last += sizeof(uint16_t);

    /* driver-specific errstr len */
    *(uint16_t *) b->last = errstr_len;
    b->last += sizeof(uint16_t);

    /* driver-specific errstr data */
    if (errstr_len) {
        b->last = ngx_copy(b->last, (u_char *) errstr, errstr_len);
    }

    /* affected rows */
    *(uint64_t *) b->last = drizzle_result_affected_rows(res);
    b->last += sizeof(uint64_t);

    /* insert id */
    *(uint64_t *) b->last = drizzle_result_insert_id(res);
    b->last += sizeof(uint64_t);

    /* column count */
    *(uint16_t *) b->last = drizzle_result_column_count(res);
    b->last += sizeof(uint16_t);

    if (b->last != b->end) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
               "diizzle: FATAL: result header buffer error");
        return NGX_ERROR;
    }

    return ngx_http_drizzle_output_chain(r, cl);
}


static ngx_int_t
ngx_http_drizzle_output_chain(ngx_http_request_t *r, ngx_chain_t *cl)
{
    ngx_http_upstream_t         *u = r->upstream;
    ngx_int_t                    rc;

    if ( ! u->header_sent ) {
        ngx_http_clear_content_length(r);

        r->headers_out.status = NGX_HTTP_OK;

        r->headers_out.content_type.data =
            (u_char *) "application/x-resty-dbd-stream";

        r->headers_out.content_type.len =
            sizeof("application/x-resty-dbd-stream") - 1;

        r->headers_out.content_type_len =
            sizeof("application/x-resty-dbd-stream") - 1;

        /* TODO: set the "X-Resty-DBD: drizzle 0.0.1" header */

        rc = ngx_http_send_header(r);

        if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        u->header_sent = 1;
    }

    rc = ngx_http_output_filter(r, cl);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    ngx_chain_update_chains(&u->free_bufs, &u->busy_bufs, &cl, u->output.tag);

    return rc;
}


ngx_int_t
ngx_http_drizzle_output_col(ngx_http_request_t *r, drizzle_column_st *col)
{
    /* TODO */
    return NGX_OK;
}


ngx_int_t
ngx_http_drizzle_output_row(ngx_http_request_t *r, uint64_t row)
{
    /* TODO */
    return NGX_OK;
}


ngx_int_t
ngx_http_drizzle_output_field(ngx_http_request_t *r, size_t offset,
        size_t len, size_t total, drizzle_field_t field)
{
    /* TODO */
    return NGX_OK;
}

