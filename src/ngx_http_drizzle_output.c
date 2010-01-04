/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_output.h"
#include "ngx_http_drizzle_processor.h"
#include "resty_dbd_stream.h"

static ngx_int_t ngx_http_drizzle_output_chain(ngx_http_request_t *r,
        ngx_chain_t *cl);

static rds_col_type_t ngx_http_drizzle_std_col_type(
        drizzle_column_type_t col_type);


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
    uint16_t                         col_count;
    ngx_int_t                        rc;

    errstr = drizzle_result_error(res);

    errstr_len = (uint16_t) strlen(errstr);

    col_count = drizzle_result_column_count(res);

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

    if (col_count == 0) {
        size += sizeof(uint16_t)    /* col list terminator */
              + sizeof(uint8_t)     /* row list terminator */
              ;
    }

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

    /* RDS format version */

    *(uint32_t *) b->last = resty_dbd_stream_version;
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
    *(uint16_t *) b->last = col_count;
    b->last += sizeof(uint16_t);

    if (col_count == 0) {
        /* column list terminator */
        *(uint16_t *) b->last = (uint16_t) 0;
        b->last += sizeof(uint16_t);

        /* row list terminator */
        *b->last++ = 0;
    }

    if (b->last != b->end) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
               "diizzle: FATAL: output result header buffer error");
        return NGX_ERROR;
    }

    rc = ngx_http_drizzle_output_chain(r, cl);

    if (rc == NGX_ERROR) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (col_count == 0) {
        return ngx_http_upstream_drizzle_done(r, u, u->peer.data);
    }

    return rc;
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
    ngx_http_upstream_t                 *u = r->upstream;
    drizzle_column_type_t                col_type = 0;
    uint16_t                             std_col_type = 0;
    const char                          *col_name = NULL;
    uint16_t                             col_name_len = 0;
    size_t                               size;
    ngx_buf_t                           *b;
    ngx_chain_t                         *cl;
    ngx_int_t                            rc;

    if (col != NULL) {
        col_type = drizzle_column_type(col);
        col_name = drizzle_column_name(col);
        col_name_len = (uint16_t) strlen(col_name);
    }

    if (col == NULL) {
        /* terminator */
        size = sizeof(uint16_t);
    } else {
        size = sizeof(uint16_t)     /* std col type */
             + sizeof(uint16_t)     /* driver-specific col type */
             + sizeof(uint16_t)     /* col name str len */
             + col_name_len         /* col name str len */
             ;
    }

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

    if (col == NULL) {
        *(uint16_t *) b->last = 0; /* 16-bit 0 terminator */
        b->last += sizeof(uint16_t);

    } else {
        /* std column type */

        std_col_type = (uint16_t) ngx_http_drizzle_std_col_type(col_type);

#if 0
        dd("std col type for %s: %d, %d (%d, %d, %d)",
                col_name, std_col_type, rds_col_type_blob,
                rds_rough_col_type_str,
                rds_rough_col_type_str << 14,
                (uint16_t) (19 | (rds_rough_col_type_str << 14))
                );
#endif

        *(uint16_t *) b->last = std_col_type;
        b->last += sizeof(uint16_t);

        /* drizzle column type */
        *(uint16_t *) b->last = col_type;
        b->last += sizeof(uint16_t);

        /* column name string length */
        *(uint16_t *) b->last = col_name_len;
        b->last += sizeof(uint16_t);

        /* column name string data */
        b->last = ngx_copy(b->last, col_name, col_name_len);
    }

    if (b->last != b->end) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
               "diizzle: FATAL: output column buffer error");
        return NGX_ERROR;
    }

    rc = ngx_http_drizzle_output_chain(r, cl);

    if (rc == NGX_ERROR) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}


ngx_int_t
ngx_http_drizzle_output_row(ngx_http_request_t *r, uint64_t row)
{
    ngx_http_upstream_t                 *u = r->upstream;
    size_t                               size;
    ngx_buf_t                           *b;
    ngx_chain_t                         *cl;
    ngx_int_t                            rc;

    size = sizeof(uint8_t);

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

    *b->last++ = (row != 0);

    rc = ngx_http_drizzle_output_chain(r, cl);

    if (rc == NGX_ERROR) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}


ngx_int_t
ngx_http_drizzle_output_field(ngx_http_request_t *r, size_t offset,
        size_t len, size_t total, drizzle_field_t field)
{
    ngx_http_upstream_t                 *u = r->upstream;
    size_t                               size;
    ngx_buf_t                           *b;
    ngx_chain_t                         *cl;
    ngx_int_t                            rc;

    if (offset == 0) {

        if (len == 0 && total != 0) {
            return NGX_DONE;
        }

        size = sizeof(uint32_t);     /* field total length */
    }

    /* (more) field data */
    size += (uint32_t) len;

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

    if (offset == 0) {
        /* field total length */
        *(uint32_t *) b->last = (uint32_t) total;
        b->last += sizeof(uint32_t);
    }

    /* field data */
    if (len) {
        b->last = ngx_copy(b->last, field, (uint32_t) len);
    }

    if (b->last != b->end) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
               "diizzle: FATAL: output field buffer error");

        return NGX_ERROR;
    }

    rc = ngx_http_drizzle_output_chain(r, cl);

    if (rc == NGX_ERROR) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}


static rds_col_type_t
ngx_http_drizzle_std_col_type(drizzle_column_type_t col_type)
{
    dd("drizzle col type: %d", col_type);

    switch (col_type) {
    case DRIZZLE_COLUMN_TYPE_DECIMAL:
        return rds_col_type_decimal;

    case DRIZZLE_COLUMN_TYPE_TINY:
        return rds_col_type_smallint;

    case DRIZZLE_COLUMN_TYPE_SHORT:
        return rds_col_type_smallint;

    case DRIZZLE_COLUMN_TYPE_LONG:
        return rds_col_type_bigint;

    case DRIZZLE_COLUMN_TYPE_FLOAT:
        return rds_col_type_real;

    case DRIZZLE_COLUMN_TYPE_DOUBLE:
        return rds_col_type_double;

    case DRIZZLE_COLUMN_TYPE_NULL:
        return rds_col_type_unknown;

    case DRIZZLE_COLUMN_TYPE_TIMESTAMP:
        return rds_col_type_timestamp;

    case DRIZZLE_COLUMN_TYPE_LONGLONG:
        return rds_col_type_bigint;

    case DRIZZLE_COLUMN_TYPE_INT24:
        return rds_col_type_smallint;

    case DRIZZLE_COLUMN_TYPE_DATE:
        return rds_col_type_timestamp;

    case DRIZZLE_COLUMN_TYPE_TIME:
        return rds_col_type_time;

    case DRIZZLE_COLUMN_TYPE_DATETIME:
        return rds_col_type_timestamp;

    case DRIZZLE_COLUMN_TYPE_YEAR:
        return rds_col_type_smallint;

    case DRIZZLE_COLUMN_TYPE_NEWDATE:
        return rds_col_type_timestamp;

    case DRIZZLE_COLUMN_TYPE_VARCHAR:
        return rds_col_type_varchar;

    case DRIZZLE_COLUMN_TYPE_BIT:
        return rds_col_type_bit;

    case DRIZZLE_COLUMN_TYPE_NEWDECIMAL:
        return rds_col_type_decimal;

    case DRIZZLE_COLUMN_TYPE_ENUM:
        return rds_col_type_varchar;

    case DRIZZLE_COLUMN_TYPE_SET:
        return rds_col_type_varchar;

    case DRIZZLE_COLUMN_TYPE_TINY_BLOB:
        return rds_col_type_blob;

    case DRIZZLE_COLUMN_TYPE_MEDIUM_BLOB:
        return rds_col_type_blob;

    case DRIZZLE_COLUMN_TYPE_LONG_BLOB:
        return rds_col_type_blob;

    case DRIZZLE_COLUMN_TYPE_BLOB:
        return rds_col_type_blob;

    case DRIZZLE_COLUMN_TYPE_VAR_STRING:
        return rds_col_type_varchar;

    case DRIZZLE_COLUMN_TYPE_STRING:
        return rds_col_type_varchar;

    case DRIZZLE_COLUMN_TYPE_GEOMETRY:
        return rds_col_type_varchar;

    default:
        return rds_col_type_unknown;
    }

    /* impossible to reach here */
    return rds_col_type_unknown;
}

