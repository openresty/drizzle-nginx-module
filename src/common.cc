#include "common.h"

void aux_ngx_http_set_location_content_handler(ngx_conf_t *cf, ngx_http_handler_pt handler)
{
	ngx_http_core_loc_conf_t *clcf;
	clcf = (ngx_http_core_loc_conf_t*)ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	clcf->handler = handler;
}

