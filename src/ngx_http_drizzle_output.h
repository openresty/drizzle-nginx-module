#ifndef NGX_HTTP_DRIZZLE_OUTPUT_H
#define NGX_HTTP_DRIZZLE_OUTPUT_H

#include <libdrizzle/drizzle_client.h>

ngx_int_t ngx_http_drizzle_output_result_header(ngx_http_request_t *r,
        drizzle_result_st *res);

ngx_int_t ngx_http_drizzle_output_col(ngx_http_request_t *r,
        drizzle_column_st *res);

ngx_int_t ngx_http_drizzle_output_row(ngx_http_request_t *r, uint64_t row);

ngx_int_t ngx_http_drizzle_output_field(ngx_http_request_t *r, size_t offset,
        size_t len, size_t total, drizzle_field_t field);

#endif /* NGX_HTTP_DRIZZLE_OUTPUT_H */

