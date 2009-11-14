#include "common.h"

// Forward declaration
static char* ngx_http_drizzle_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_drizzle_handler(ngx_http_request_t *r);
static void* ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf);
static char* ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

/// MySQL service port bounds for configuration validation
static ngx_conf_num_bounds_t db_port_bounds = {
	ngx_conf_check_num_bounds,	// post_handler
	1,	// low
	65535,	// high
};

/// Nginx directives for module drizzle
static ngx_command_t ngx_http_drizzle_cmds[] = {
	{	// "drizzle" directive enable mod_drizzle content handler for current location
		ngx_string("drizzle"),	// name
		NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,	// type
		ngx_http_drizzle_cmd,	// set
		0,	// conf
		0,	// offset
		NULL	// post
	},
	{	// "drizzle_host" directive set backend MySQL service hostname for current location
		// NOTE: default value is "localhost"
		ngx_string("drizzle_host"),
		NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_drizzle_loc_conf_t, db_host),
		NULL
	},
	{	// "drizzle_port" directive set backend MySQL service port for current location
		ngx_string("drizzle_port"),
		NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_drizzle_loc_conf_t, db_port),
		&db_port_bounds	// Validate service port range
	},

	ngx_null_command
};

// Nginx HTTP subsystem module hooks
static ngx_http_module_t ngx_http_drizzle_module_ctx = {
	NULL,	// preconfiguration
	NULL,	// postconfiguration

	NULL,	// create_main_conf
	NULL,	// merge_main_conf

	NULL,	// create_srv_conf
	NULL,	// merge_srv_conf

	ngx_http_drizzle_create_loc_conf,	// create_loc_conf
	ngx_http_drizzle_merge_loc_conf	// merge_loc_conf
};

extern "C" ngx_module_t ngx_http_drizzle_module;

ngx_module_t ngx_http_drizzle_module = {
	NGX_MODULE_V1,
	&ngx_http_drizzle_module_ctx,	// module context
	ngx_http_drizzle_cmds,	// module directives
	NGX_HTTP_MODULE,	// module type
	NULL,	// init master
	NULL,	// init module
	NULL,	// init process
	NULL,	// init thread
	NULL,	// exit thread
	NULL,	// exit process
	NULL,	// exit master
	NGX_MODULE_V1_PADDING
};


static void* ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_drizzle_loc_conf_t *conf;
	conf = (ngx_http_drizzle_loc_conf_t*)ngx_pcalloc(cf->pool, sizeof(ngx_http_drizzle_loc_conf_t));
	if(conf == NULL) {
		return NULL;
	}

	conf->db_host.data = NULL;
	conf->db_port = NGX_CONF_UNSET;

	return conf;
}

static char* ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_drizzle_loc_conf_t *pcf = (ngx_http_drizzle_loc_conf_t*)parent;
	ngx_http_drizzle_loc_conf_t *ccf = (ngx_http_drizzle_loc_conf_t*)child;

	ngx_conf_merge_str_value(ccf->db_host, pcf->db_host, "localhost");
	ngx_conf_merge_value(ccf->db_port, pcf->db_port, 3306);

	return NGX_CONF_OK;
}

static char* ngx_http_drizzle_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	aux_ngx_http_set_location_content_handler(cf, ngx_http_drizzle_handler);
	return NGX_CONF_OK;
}

static ngx_int_t ngx_http_drizzle_handler(ngx_http_request_t *r)
{
	ngx_int_t rc;
	ngx_buf_t *b;
	ngx_chain_t out;

	rc = ngx_http_discard_request_body(r);
	if(rc != NGX_OK) {
		return rc;
	}

	r->headers_out.content_type_len = sizeof("text/plain") - 1;
	r->headers_out.content_type.len = sizeof("text/plain") - 1;
	r->headers_out.content_type.data = (u_char*)"text/plain";

	b = (ngx_buf_t*)ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if(!b) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	out.buf = b;
	out.next = NULL;

#define STR "Hello, mod_drizzle!"
	b->pos = (u_char*)STR;
	b->last = b->pos + sizeof(STR) - 1;
	b->memory = 1;
	b->last_buf = 1;

	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = sizeof(STR) - 1;

	rc = ngx_http_send_header(r);
	if(rc == NGX_ERROR || rc > NGX_OK) {
		return rc;
	}

	return ngx_http_output_filter(r, &out);
}

