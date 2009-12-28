/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_drizzle_output.h"

ngx_int_t
ngx_http_drizzle_output_result_header(ngx_http_request_t *r,
        drizzle_result_st *res)
{
    /* TODO */
    return NGX_OK;
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

