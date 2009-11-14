#ifndef MOD_DRIZZLE_COMMON_H__
#define MOD_DRIZZLE_COMMON_H__

extern "C" {
//#include <ngx_config.h>
//#include <ngx_core.h>
#include <ngx_http.h>
}

// Per-location configuration for module drizzle
struct ngx_http_drizzle_loc_conf_t {
	ngx_str_t db_host;	//< MySQL service hostname for current location
	ngx_int_t db_port;	//< MySQL service port for current location
};

extern void aux_ngx_http_set_location_content_handler(ngx_conf_t *cf, ngx_http_handler_pt handler);

#endif

