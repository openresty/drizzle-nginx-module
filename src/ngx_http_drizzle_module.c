/* Copyright (C) chaoslawful */
/* Copyright (C) agentzh */

#define DDEBUG 1
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"

/* drizzle service port bounds for configuration validation */
enum {
    drizzle_min_port = 1,
    drizzle_max_port = 65535
};


/* Forward declaration */

/* main content handler */
static ngx_int_t ngx_http_drizzle_handler(ngx_http_request_t *r);

/* for read/write event handlers */
static ngx_int_t ngx_http_drizzle_rw_handler(ngx_http_request_t *r,
        ngx_http_upstream_t *u);

/* just a work-around to override the default u->output_filter */
static ngx_int_t ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in);

static void ngx_http_drizzle_event_handler(ngx_event_t *ev);

static ngx_int_t ngx_http_drizzle_do_process(ngx_http_request_t *r,
        ngx_http_drizzle_ctx_t *ctx);

static char *
ngx_http_drizzle_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

/*
static char* ngx_http_drizzle_dbname(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char* ngx_http_drizzle_query(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
*/

static char* ngx_http_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void* ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf);
static char* ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent,
        void *child);


/* config directives for module drizzle */
static ngx_command_t ngx_http_drizzle_cmds[] = {
    {
        ngx_string("drizzle_query"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
            |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_drizzle_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, query),
        NULL
    },
    {
        ngx_string("drizzle_dbname"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
            |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
        ngx_http_drizzle_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, dbname),
        NULL
    },
    {
        ngx_string("drizzle_server"),
        NGX_HTTP_UPS_CONF|NGX_CONF_1MORE,
        ngx_http_drizzle_server,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    ngx_null_command
};


/* Nginx HTTP subsystem module hooks */
static ngx_http_module_t ngx_http_drizzle_module_ctx = {
    NULL,    /* preconfiguration */
    NULL,    /* postconfiguration */

    NULL,    /* create_main_conf */
    NULL,    /* merge_main_conf */

    NULL,    /* create_srv_conf */
    NULL,    /* merge_srv_conf */

    ngx_http_drizzle_create_loc_conf,    /* create_loc_conf */
    ngx_http_drizzle_merge_loc_conf      /* merge_loc_conf */
};


ngx_module_t ngx_http_drizzle_module = {
    NGX_MODULE_V1,
    &ngx_http_drizzle_module_ctx,       /* module context */
    ngx_http_drizzle_cmds,              /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,    /* init master */
    NULL,    /* init module */
    NULL,    /* init process */
    NULL,    /* init thread */
    NULL,    /* exit thread */
    NULL,    /* exit process */
    NULL,    /* exit master */
    NGX_MODULE_V1_PADDING
};


static void*
ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_drizzle_loc_conf_t             *conf;

    conf = ngx_palloc(cf->pool, sizeof(ngx_http_drizzle_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->dbname = NGX_CONF_UNSET_PTR;
    conf->query  = NGX_CONF_UNSET_PTR;

    return conf;
}


static char*
ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_drizzle_loc_conf_t *prev = parent;
    ngx_http_drizzle_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->dbname, prev->dbname, NULL);
    ngx_conf_merge_ptr_value(conf->query, prev->query, NULL);

    return NGX_CONF_OK;
}


static char *
ngx_http_drizzle_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    char                             *p = conf;
    ngx_http_complex_value_t        **field;
    ngx_str_t                        *value;
    ngx_conf_post_t                  *post;
    ngx_http_compile_complex_value_t  ccv;

    field = (ngx_http_complex_value_t **) (p + cmd->offset);

    if (*field) {
        return "is duplicate";
    }

    *field = ngx_pcalloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (*field == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    if (value[1].len == 0) {
        return NGX_OK;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = *field;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, field);
    }

    return NGX_CONF_OK;
}


static char* ngx_http_drizzle_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_drizzle_handler;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_drizzle_handler(ngx_http_request_t *r)
{
    ngx_http_drizzle_ctx_t *ctx = (ngx_http_drizzle_ctx_t*)ngx_http_get_module_ctx(r, ngx_http_drizzle_module);

    if(!ctx) {
        /* no context attached to current request, create and init one */
        ctx = (ngx_http_drizzle_ctx_t*)ngx_pcalloc(r->pool, sizeof(ngx_http_drizzle_ctx_t));

        if(!ctx) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "failed to allocate drizzle context: ngx_pcalloc returns NULL");
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_drizzle_module);

        ctx->state = state_db_init;
        ctx->ngx_db_con = NULL;

        ngx_http_drizzle_loc_conf_t *dlcf = (ngx_http_drizzle_loc_conf_t*)ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);
        if(dlcf->sql_lengths) {
            /* given raw sql have nginx variables inside, need to expand them */
            if(ngx_http_script_run(r, &(ctx->sql), dlcf->sql_lengths->elts, 0, dlcf->sql_values->elts) == NULL) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "failed to expand nginx variables in the given SQL");
                return NGX_ERROR;
            }

            dd("SQL to be executed: '%.*s'", ctx->sql.len, ctx->sql.data);
        } else {
            dd("given raw sql have no embedded nginx variables, use it as is.");

            ctx->sql = dlcf->raw_sql;
        }
    }

#if defined(nginx_version) && nginx_version >= 8011

    /* increase main request's reference count */
    r->main->count++;

#endif

    return process_drizzle(r, ctx);
}

static void drizzle_io_event_handler(ngx_event_t *ev)
{
    ngx_connection_t *c;
    ngx_http_request_t *r;
    ngx_http_drizzle_ctx_t *ctx;

    dd("drizzle io event handler");

    c = (ngx_connection_t*)(ev->data);
    r = (ngx_http_request_t*)(c->data);
    ctx = (ngx_http_drizzle_ctx_t*)ngx_http_get_module_ctx(r, ngx_http_drizzle_module);

    if(ctx) {
        /* libdrizzle use standard poll() event constants, and depends on drizzle_con_wait() to fill them, */
		/* here we can directly call drizzle_con_wait() to fill libdrizzle internal event states, */
		/* as poll() will play well along with epoll() or other event mechanisms used by nginx */
		(void)drizzle_con_wait(&(ctx->dr));
    } else {
        dd("No context!");
    }

    process_drizzle(r, ctx);
}

static ngx_int_t process_drizzle(ngx_http_request_t *r, ngx_http_drizzle_ctx_t *ctx)
{
    ngx_http_drizzle_loc_conf_t *dlcf;
    drizzle_return_t rc;
    drizzle_column_st *col;
    drizzle_row_t row;
    proc_state_t old_state = ctx->state;

    dd("process drizzle");

    dlcf = (ngx_http_drizzle_loc_conf_t*)ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    for(;;) {
        switch(ctx->state) {
            case state_db_init:
                dd("initialize libdrizzle client");

                (void)drizzle_create(&(ctx->dr));
                drizzle_add_options(&(ctx->dr), DRIZZLE_NON_BLOCKING);
                drizzle_con_create(&(ctx->dr), &(ctx->dr_con));
                drizzle_con_add_options(&(ctx->dr_con), DRIZZLE_CON_MYSQL);
                {
                    char tmp1[1024], tmp2[1024];

                    ngx_memcpy(tmp1, dlcf->db_name.data, dlcf->db_name.len);
                    tmp1[dlcf->db_name.len] = '\0';
                    drizzle_con_set_db(&(ctx->dr_con), tmp1);

                    ngx_memcpy(tmp1, dlcf->db_host.data, dlcf->db_host.len);
                    tmp1[dlcf->db_host.len] = '\0';
                    drizzle_con_set_tcp(&(ctx->dr_con), tmp1, dlcf->db_port);

                    ngx_memcpy(tmp1, dlcf->db_user.data, dlcf->db_user.len);
                    tmp1[dlcf->db_user.len] = '\0';
                    ngx_memcpy(tmp2, dlcf->db_pass.data, dlcf->db_pass.len);
                    tmp2[dlcf->db_pass.len] = '\0';
                    drizzle_con_set_auth(&(ctx->dr_con), tmp1, tmp2);
                }

                {
                    /* clear content length, so nginx will use chunked encoding for output */
                    ngx_http_clear_content_length(r);

                    /* set response header and send it */
                    r->headers_out.status = NGX_HTTP_OK;
                    r->headers_out.content_type.data = (u_char*)"text/plain";
                    r->headers_out.content_type.len = sizeof("text/plain") - 1;
                    r->headers_out.content_type_len = sizeof("text/plain") - 1;

                    if(ngx_http_send_header(r) != NGX_OK) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to send response headers!");
                        ctx->state = state_db_err;
                        continue;
                    }
                }

                ctx->state = state_db_connect;
                continue;

            case state_db_connect:
                dd("connect to database");

                rc = drizzle_con_connect(&(ctx->dr_con));

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    dd("well, still IO wait...");

                    int db_con_fd = drizzle_con_fd(&(ctx->dr_con));

                    if(db_con_fd == -1) {
                        dd("error occured, finish query");

                        ctx->state = state_db_err;
                        continue;
                    }

                    dd("setup an nginx event related to db connection fd");

                    if(!(ctx->ngx_db_con)) {
                        dd("no connection found...");
                        ctx->ngx_db_con = ngx_get_connection(db_con_fd, r->connection->log);

                        if(!(ctx->ngx_db_con)) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to get a free nginx connection structure!");

                            dd("close established drizzle connection");

                            drizzle_con_close(&(ctx->dr_con));
                            ctx->state = state_db_err;
                            continue;
                        }

                        /* we don't need to setup most of the connection fields, because */
                        /* they're only useful to nginx i/o apis. */
                        /* here we only need the connection event functionality. */

                        ctx->ngx_db_con->log_error = r->connection->log_error;
                        ctx->ngx_db_con->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

                        ctx->ngx_db_con->data = r;
                        ctx->ngx_db_con->log = r->connection->log;
                        ctx->ngx_db_con->read->log = r->connection->log;
                        ctx->ngx_db_con->read->handler = drizzle_io_event_handler;
                        ctx->ngx_db_con->read->data = ctx->ngx_db_con;
                        ctx->ngx_db_con->write->log = r->connection->log;
                        ctx->ngx_db_con->write->handler = drizzle_io_event_handler;
                        ctx->ngx_db_con->write->data = ctx->ngx_db_con;

                        if(ngx_add_conn(ctx->ngx_db_con) == NGX_ERROR) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to add database connection into nginx event pool!");

                            ctx->state = state_db_err;
                            continue;
                        }
                    }

                    dd("got here...");

                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    dd("error occured, finish query");

                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to connect database: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->state = state_db_err;
                    continue;
                }

                ctx->state = state_db_send_query;
                continue;

            case state_db_send_query:
                dd("send query to database");

                (void)drizzle_query(&(ctx->dr_con), &(ctx->drizzle_res), (const char*)(ctx->sql.data), ctx->sql.len, &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to query database: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->state = state_db_err;
                    continue;
                }

                {
                    size_t tmp_len;
                    ngx_buf_t *b;
                    ngx_chain_t out;

                    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                    if(!b) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate buffer for response body!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    out.buf = b;
                    out.next = NULL;

                    tmp_len = sizeof("Result:\n") - 1
                        + sizeof("\tSQL state:\t0123456\n") - 1
                        + sizeof("\tColumn count:\t0123456789\n") - 1
                        + sizeof("\tRow count:\t0123456789\n") - 1;

                    b->pos = ngx_pcalloc(r->pool, tmp_len);
                    if(!b->pos) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate buffer for response body!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    b->last = ngx_snprintf(b->pos, tmp_len,
                            "Result:\n"
                            "\tSQL state:\t%s\n"
                            "\tColumn count:\t%ud\n"
                            "\tRow count:\t%uL\n",
                            drizzle_result_sqlstate(&(ctx->drizzle_res)),
                            drizzle_result_column_count(&(ctx->drizzle_res)),
                            drizzle_result_row_count(&(ctx->drizzle_res)));
                    b->temporary = 1;    /* allow http write filter to free the buf when outputing done */

                    ngx_http_output_filter(r, &out);
                }

                dd("Result:");
                dd("\tSQL state:\t%s", drizzle_result_sqlstate(&(ctx->drizzle_res)));
                dd("\tColumn count:\t%d", drizzle_result_column_count(&(ctx->drizzle_res)));
                dd("\tRow count:\t%llu", drizzle_result_row_count(&(ctx->drizzle_res)));

                if(drizzle_result_column_count(&(ctx->drizzle_res)) == 0) {
                    /* no more data to be read */
                    ctx->state = state_db_fin;
                    continue;
                }

                ctx->state = state_db_recv_fields;
                continue;

            case state_db_recv_fields:
                dd("receive result status and field descriptions");

                col = drizzle_column_read(&(ctx->drizzle_res), NULL, &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to recv field description data: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->state = state_db_err;
                    continue;
                }

                if(!col) {
                    /* no more field descriptions to read */
                    ctx->state = state_db_recv_rows;
                    continue;
                }

                {
                    size_t tmp_len;
                    ngx_buf_t *b;
                    ngx_chain_t out;

                    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                    if(!b) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate buffer for response body!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    out.buf = b;
                    out.next = NULL;

                    tmp_len = sizeof("Field:\n") - 1
                        + sizeof("\tField name:\t0123456789\n") - 1
                        + sizeof("\tField size:\t0123456789\n") - 1
                        + sizeof("\tField type:\t0123456789\n") - 1;

                    b->pos = ngx_pcalloc(r->pool, tmp_len);
                    if(!b->pos) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate buffer for response body!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    b->last = ngx_snprintf(b->pos, tmp_len,
                            "Field:\n"
                            "\tField name:\t%s\n"
                            "\tField size:\t%ud\n"
                            "\tField type:\t%ud\n",
                            drizzle_column_name(col),
                            drizzle_column_size(col),
                            (int)drizzle_column_type(col));
                    b->temporary = 1;    /* allow http output filter to free the buf when outputing done */

                    ngx_http_output_filter(r, &out);
                }

                dd("Field:");
                dd("\tField name:\t%s", drizzle_column_name(col));
                dd("\tField size:\t%u", drizzle_column_size(col));
                dd("\tField type:\t%d", (int)drizzle_column_type(col));

                drizzle_column_free(col);

                /* continue reading field descriptions */
                continue;

            case state_db_recv_rows:
                dd("receive result data rows");

                row = drizzle_row_buffer(&(ctx->drizzle_res), &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to recv row data: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->state = state_db_err;
                    continue;
                }

                if(!row) {
                    /* no more rows to read */
                    ctx->state = state_db_fin;
                    continue;
                }

                /* TODO: added testing response here! */
                {
                    ngx_buf_t *title_buf;
                    ngx_chain_t *out_lst, *head;
                    size_t *field_sizes = drizzle_row_field_sizes(&(ctx->drizzle_res));
                    uint16_t i;
                    size_t tmp_len;

                    head = out_lst = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
                    if(!out_lst) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output chain node!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    title_buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                    if(!title_buf) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output buffer node!");
                        ctx->state = state_db_err;
                        continue;
                    }

                    tmp_len = sizeof("Row 0123456789:\n") - 1;
                    title_buf->pos = ngx_pcalloc(r->pool, tmp_len);
                    if(!(title_buf->pos)) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output buffer!");
                        ctx->state = state_db_err;
                        continue;
                    }
                    title_buf->last = ngx_snprintf(title_buf->pos, tmp_len,
                            "Row %L:\n", drizzle_row_current(&(ctx->drizzle_res)));
                    /* the buffer is newly allocated and its content can be changed freely */
                    title_buf->temporary = 1;

                    out_lst->buf = title_buf;
                    out_lst->next = 0;

                    for(i = 0; i < drizzle_result_column_count(&(ctx->drizzle_res)); ++i) {
                        ngx_buf_t *field_buf;
                        ngx_chain_t *new_lst = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));

                        if(!new_lst) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to allocate output chain node: row=%d, col=%d",
                                    drizzle_row_current(&(ctx->drizzle_res)), i);
                            ctx->state = state_db_err;
                            /* we will free all drizzle resources, so don't need to call drizzle_row_free() here */
                            continue;
                        }

                        field_buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                        if(!field_buf) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to allocate output buffer node: row=%d, col=%d",
                                    drizzle_row_current(&(ctx->drizzle_res)), i);
                            ctx->state = state_db_err;
                            continue;
                        }

                        if(!row[i]) {
                            tmp_len = sizeof("\t(NULL)\n") - 1;
                            field_buf->pos = ngx_pcalloc(r->pool, tmp_len);
                            if(!(field_buf->pos)) {
                                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                        "failed to allocate output buffer: row=%d, col=%d",
                                        drizzle_row_current(&(ctx->drizzle_res)), i);
                                ctx->state = state_db_err;
                                continue;
                            }

                            field_buf->last = ngx_cpymem(field_buf->pos, "\t(NULL)\n", tmp_len);
                            /* the buffer is newly allocated and its content can be changed freely */
                            field_buf->temporary = 1;
                        } else {
                            tmp_len = 4096;
                            field_buf->pos = ngx_pcalloc(r->pool, tmp_len);
                            if(!(field_buf->pos)) {
                                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                        "failed to allocate output buffer: row=%d, col=%d",
                                        drizzle_row_current(&(ctx->drizzle_res)), i);
                                ctx->state = state_db_err;
                                continue;
                            }

                            field_buf->last = ngx_snprintf(field_buf->pos, tmp_len,
                                    "\t(%d) %*s\n", field_sizes[i], field_sizes[i], row[i]);
                            /* the buffer is newly allocated and its content can be changed freely */
                            field_buf->temporary = 1;
                        }

                        new_lst->buf = field_buf;
                        new_lst->next = 0;
                        out_lst->next = new_lst;
                        out_lst = new_lst;
                    }

                    /* XXX: we set last buffer's 'flush' flag instead of calling */
                    /* ngx_http_send_special(r, NGX_HTTP_FLUSH) to prevent traversing */
                    /* output filter chain twice */
                    out_lst->buf->flush = 1;
                    ngx_http_output_filter(r, head);
                }

                dd("Row %llu:", drizzle_row_current(&(ctx->drizzle_res)));
                {
                    size_t *field_sizes;
                    uint16_t i;

                    field_sizes = drizzle_row_field_sizes(&(ctx->drizzle_res));
                    for(i = 0; i < drizzle_result_column_count(&(ctx->drizzle_res)); ++i) {
                        if(!row[i]) {
                            dd("\t(NULL)");
                        } else {
                            dd("\t(%u) %*s", field_sizes[i], field_sizes[i], row[i]);
                        }
                    }
                }

                drizzle_row_free(&(ctx->drizzle_res), row);

                /* continue reading rows */
                continue;

            case state_db_fin:
            case state_db_err:
                dd("remove db connection from event pool");

                if(ngx_del_conn(ctx->ngx_db_con, NGX_CLOSE_EVENT) != NGX_OK) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to remove database connection from nginx event pool!");
                }
                ngx_free_connection(ctx->ngx_db_con);

                if(ctx->state == state_db_err) {
                    ngx_chain_t out;
                    ngx_buf_t *b = ngx_calloc_buf(r->pool);
                    if(b) {
                        b->pos = ngx_palloc(r->pool, 4096);
                        b->last = ngx_snprintf(b->pos, 4096, "*** ERROR: %s\n",
                                drizzle_error(&(ctx->dr)));
                        b->last_buf = 1;
                        b->flush = 1;
                        b->temporary = 1;
                    }
                    out.buf = b;
                    out.next = NULL;
                    ngx_http_output_filter(r, &out);
                } else {
                    /* prompt nginx for the end of output contents and flush all contents not sending yet */
                    ngx_http_send_special(r, NGX_HTTP_LAST | NGX_HTTP_FLUSH);
                }

                /* finalize libdrizzle client, free connection and query result */
                drizzle_free(&(ctx->dr));

                /* manually finalize request only when the entering state is state_db_init, */
                /* cause that call is not from our registered event handler but NginX */
                /* content phase handler, and it will finalize request after calling us. */
                if(old_state != state_db_init) {
                    /* finalize current request manually  */
                    ngx_http_finalize_request(r, (ctx->state == state_db_fin) ? NGX_HTTP_OK : NGX_HTTP_INTERNAL_SERVER_ERROR);
                }

                return (ctx->state == state_db_fin) ? NGX_OK : NGX_ERROR;

            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "invalid mod_drizzle processing state: cur_state=%d", ctx->state);
                return NGX_ERROR;
        }

        break;
    }

    dd("returning NGX_DONE...");

    return NGX_DONE;
}

