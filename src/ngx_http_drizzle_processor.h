#ifndef NGX_HTTP_DRIZZLE_PROCESSOR_H
#define NGX_HTTP_DRIZZLE_PROCESSOR_H

#include <ngx_http.h>
#include <ngx_core.h>

ngx_int_t ngx_http_drizzle_process_events(ngx_http_request_t *r);

void ngx_http_drizzle_event_handler(ngx_event_t *ev);

#endif /* NGX_HTTP_DRIZZLE_PROCESSOR_H */

