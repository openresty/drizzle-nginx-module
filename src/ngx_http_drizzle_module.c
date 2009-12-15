#include "ngx_http_drizzle_module.h"

/* Forward declaration */
static ngx_int_t ngx_http_drizzle_handler(ngx_http_request_t *r);

static char* ngx_http_drizzle_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char* ngx_http_drizzle_sql_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void drizzle_io_event_handler(ngx_event_t *ev);
static ngx_int_t process_drizzle(ngx_http_request_t *r, ngx_http_drizzle_ctx_t *ctx);

static void* ngx_http_drizzle_create_loc_conf(ngx_conf_t *cf);
static char* ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

/* MySQL service port bounds for configuration validation */
static ngx_conf_num_bounds_t db_port_bounds = {
    ngx_conf_check_num_bounds,    /* post_handler */
    1,    /* low */
    65535,    /* high */
};

/* Nginx directives for module drizzle */
static ngx_command_t ngx_http_drizzle_cmds[] = {
    {    /* "drizzle" directive enable mod_drizzle content handler for current location */
        ngx_string("drizzle"),    /* name */
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,    /* type */
        ngx_http_drizzle_cmd,    /* set */
        0,    /* conf */
        0,    /* offset */
        NULL    /* post */
    },
    {    /* "drizzle_host" directive set backend MySQL service hostname for current location */
        /* NOTE: default value is "localhost" */
        ngx_string("drizzle_host"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, db_host),
        NULL
    },
    {    /* "drizzle_port" directive set backend MySQL service port for current location */
        ngx_string("drizzle_port"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, db_port),
        &db_port_bounds    /* Validate service port range */
    },
    {    /* "drizzle_user" directive set user for MySQL service */
        ngx_string("drizzle_user"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, db_user),
        NULL
    },
    {    /* "drizzle_pass" directive set password for MySQL service */
        ngx_string("drizzle_pass"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, db_pass),
        NULL
    },
    {    /* "drizzle_db" directive set database name for MySQL service */
        ngx_string("drizzle_db"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_drizzle_loc_conf_t, db_name),
        NULL
    },
    {    /* "drizzle_sql" directive set SQL statement to execute */
        /* support variable expanding */
        ngx_string("drizzle_sql"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_http_drizzle_sql_cmd,
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
    ngx_http_drizzle_merge_loc_conf    /* merge_loc_conf */
};

ngx_module_t ngx_http_drizzle_module = {
    NGX_MODULE_V1,
    &ngx_http_drizzle_module_ctx,    /* module context */
    ngx_http_drizzle_cmds,    /* module directives */
    NGX_HTTP_MODULE,    /* module type */
    NULL,    /* init master */
    NULL,    /* init module */
    NULL,    /* init process */
    NULL,    /* init thread */
    NULL,    /* exit thread */
    NULL,    /* exit process */
    NULL,    /* exit master */
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
    conf->db_user.data = NULL;
    conf->db_pass.data = NULL;
    conf->db_name.data = NULL;
    conf->raw_sql.data = NULL;
    conf->sql_lengths = NULL;
    conf->sql_values = NULL;

    return conf;
}

static char* ngx_http_drizzle_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_drizzle_loc_conf_t *pcf = (ngx_http_drizzle_loc_conf_t*)parent;
    ngx_http_drizzle_loc_conf_t *ccf = (ngx_http_drizzle_loc_conf_t*)child;

    ngx_conf_merge_str_value(ccf->db_host, pcf->db_host, "localhost");
    ngx_conf_merge_value(ccf->db_port, pcf->db_port, 3306);
    ngx_conf_merge_str_value(ccf->db_user, pcf->db_user, "root");
    ngx_conf_merge_str_value(ccf->db_pass, pcf->db_pass, "");
    ngx_conf_merge_str_value(ccf->db_name, pcf->db_name, "test");
    ngx_conf_merge_str_value(ccf->raw_sql, pcf->raw_sql, "");

    DD("Database host: '%.*s'", ccf->db_host.len, ccf->db_host.data);
    DD("Database port: %d", ccf->db_port);
    DD("Database user: '%.*s'", ccf->db_user.len, ccf->db_user.data);
    DD("Database password: '%.*s'", ccf->db_pass.len, ccf->db_pass.data);
    DD("Database name: '%.*s'", ccf->db_name.len, ccf->db_name.data);
    DD("Raw SQL: '%.*s'", ccf->raw_sql.len, ccf->raw_sql.data);

    return NGX_CONF_OK;
}

static char* ngx_http_drizzle_sql_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_drizzle_loc_conf_t *dlcf = (ngx_http_drizzle_loc_conf_t*)conf;
    ngx_str_t *value, *sql;
    ngx_uint_t n;
    ngx_http_script_compile_t sc;

    value = (ngx_str_t*)(cf->args->elts);
    sql = &value[1];
    n = ngx_http_script_variables_count(sql);
    DD("Found %d NginX variables in the raw SQL statement: %.*s",
            n, sql->len, sql->data);

    dlcf->raw_sql = *sql;
    dlcf->sql_lengths = NULL;
    dlcf->sql_values = NULL;

    if(n) {
        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = sql;
        sc.lengths = &(dlcf->sql_lengths);
        sc.values = &(dlcf->sql_values);
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        if(ngx_http_script_compile(&sc) != NGX_OK) {
            return (char*)NGX_CONF_ERROR;
        }
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

        ctx->cur_state = DB_INIT;
        ctx->ngx_db_con = NULL;

        ngx_http_drizzle_loc_conf_t *dlcf = (ngx_http_drizzle_loc_conf_t*)ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);
        if(dlcf->sql_lengths) {
            /* given raw sql have nginx variables inside, need to expand them */
            if(ngx_http_script_run(r, &(ctx->sql), dlcf->sql_lengths->elts, 0, dlcf->sql_values->elts) == NULL) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "failed to expand nginx variables in the given SQL");
                return NGX_ERROR;
            }

            DD("SQL to be executed: '%.*s'", ctx->sql.len, ctx->sql.data);
        } else {
            /* given raw sql have no embedded nginx variables, use it as is */
            ctx->sql = dlcf->raw_sql;
        }
    }

#if defined(nginx_version) && nginx_version >= 8000
    /* increase main request's reference count */
    ++(r->main->count);
#endif

    return process_drizzle(r, ctx);
}

static void drizzle_io_event_handler(ngx_event_t *ev)
{
    ngx_connection_t *c;
    ngx_http_request_t *r;
    ngx_http_drizzle_ctx_t *ctx;

    c = (ngx_connection_t*)(ev->data);
    r = (ngx_http_request_t*)(c->data);
    ctx = (ngx_http_drizzle_ctx_t*)ngx_http_get_module_ctx(r, ngx_http_drizzle_module);

    if(ctx) {
        /* libdrizzle use standard poll() event constants, and depends on drizzle_con_wait() to fill them, */
        /* so we must explicitly set the drizzle connection event flags. */
        short revents = 0;
        if(ev == c->read) {
            /* read event */
            revents |= POLLIN;
        } else if(ev == c->write) {
            /* write event */
            revents |= POLLOUT;
        }
        
        /* drizzle_con_set_revents() isn't declared external in libdrizzle-0.4.0, */
        /* so we have to do its job all by ourselves... */
        if(revents != 0) {
            ctx->dr_con.options |= DRIZZLE_CON_IO_READY;
        }
        ctx->dr_con.revents = revents;
        ctx->dr_con.events &= (short)~revents;
    }

    process_drizzle(r, ctx);
}

static ngx_int_t process_drizzle(ngx_http_request_t *r, ngx_http_drizzle_ctx_t *ctx)
{
    ngx_http_drizzle_loc_conf_t *dlcf;
    drizzle_return_t rc;
    drizzle_column_st *col;
    drizzle_row_t row;
    proc_state_t old_state = ctx->cur_state;

    dlcf = (ngx_http_drizzle_loc_conf_t*)ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    for(;;) {
        switch(ctx->cur_state) {
            case DB_INIT:
                /* initialize libdrizzle client */
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
                        ctx->cur_state = DB_ERR;
                        continue;
                    }
                }

                ctx->cur_state = DB_CONNECT;
                continue;

            case DB_CONNECT:
                /* connect to database */
                rc = drizzle_con_connect(&(ctx->dr_con));

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    int db_con_fd = drizzle_con_fd(&(ctx->dr_con));

                    if(db_con_fd == -1) {
                        /* error occured, finish query */
                        ctx->cur_state = DB_ERR;
                        continue;
                    }

                    /* setup a nginx event related to db connection fd */
                    if(!(ctx->ngx_db_con)) {
                        ctx->ngx_db_con = ngx_get_connection(db_con_fd, r->connection->log);

                        if(!(ctx->ngx_db_con)) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to get a free nginx connection structure!");

                            /* close established drizzle connection */
                            drizzle_con_close(&(ctx->dr_con));
                            ctx->cur_state = DB_ERR;
                            continue;
                        }

                        /* we don't need to setup most of the connection fields, because */
                        /* they're only useful to nginx i/o apis. */
                        /* here we only need the connection event functionality. */
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

                            ctx->cur_state = DB_ERR;
                            continue;
                        }
                    }

                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to connect database: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->cur_state = DB_ERR;
                    continue;
                }

                ctx->cur_state = DB_SEND_QUERY;
                continue;

            case DB_SEND_QUERY:
                /* send query to database */
                (void)drizzle_query(&(ctx->dr_con), &(ctx->dr_res), (const char*)(ctx->sql.data), ctx->sql.len, &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to query database: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->cur_state = DB_ERR;
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
                        ctx->cur_state = DB_ERR;
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
                        ctx->cur_state = DB_ERR;
                        continue;
                    }

                    b->last = ngx_snprintf(b->pos, tmp_len,
                            "Result:\n"
                            "\tSQL state:\t%s\n"
                            "\tColumn count:\t%ud\n"
                            "\tRow count:\t%uL\n",
                            drizzle_result_sqlstate(&(ctx->dr_res)),
                            drizzle_result_column_count(&(ctx->dr_res)),
                            drizzle_result_row_count(&(ctx->dr_res)));
                    b->temporary = 1;    /* allow http write filter to free the buf when outputing done */

                    ngx_http_output_filter(r, &out);
                }

                DD("Result:");
                DD("\tSQL state:\t%s", drizzle_result_sqlstate(&(ctx->dr_res)));
                DD("\tColumn count:\t%d", drizzle_result_column_count(&(ctx->dr_res)));
                DD("\tRow count:\t%llu", drizzle_result_row_count(&(ctx->dr_res)));
                
                if(drizzle_result_column_count(&(ctx->dr_res)) == 0) {
                    /* no more data to be read */
                    ctx->cur_state = DB_FIN;
                    continue;
                }

                ctx->cur_state = DB_RECV_FIELDS;
                continue;

            case DB_RECV_FIELDS:
                /* receive result status and field descriptions */
                col = drizzle_column_read(&(ctx->dr_res), NULL, &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to recv field description data: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->cur_state = DB_ERR;
                    continue;
                }

                if(!col) {
                    /* no more field descriptions to read */
                    ctx->cur_state = DB_RECV_ROWS;
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
                        ctx->cur_state = DB_ERR;
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
                        ctx->cur_state = DB_ERR;
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

                DD("Field:");
                DD("\tField name:\t%s", drizzle_column_name(col));
                DD("\tField size:\t%u", drizzle_column_size(col));
                DD("\tField type:\t%d", (int)drizzle_column_type(col));

                drizzle_column_free(col);

                /* continue reading field descriptions */
                continue;

            case DB_RECV_ROWS:
                /* receive result data rows */
                row = drizzle_row_buffer(&(ctx->dr_res), &rc);

                if(rc == DRIZZLE_RETURN_IO_WAIT) {
                    break;
                } else if(rc != DRIZZLE_RETURN_OK) {
                    /* error occured, finish query */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to recv row data: (%d) %s", rc, drizzle_error(&(ctx->dr)));

                    ctx->cur_state = DB_ERR;
                    continue;
                }

                if(!row) {
                    /* no more rows to read */
                    ctx->cur_state = DB_FIN;
                    continue;
                }

                /* TODO: added testing response here! */
                {
                    ngx_buf_t *title_buf;
                    ngx_chain_t *out_lst, *head;
                    size_t *field_sizes = drizzle_row_field_sizes(&(ctx->dr_res));
                    uint16_t i;
                    size_t tmp_len;

                    head = out_lst = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
                    if(!out_lst) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output chain node!");
                        ctx->cur_state = DB_ERR;
                        continue;
                    }

                    title_buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                    if(!title_buf) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output buffer node!");
                        ctx->cur_state = DB_ERR;
                        continue;
                    }

                    tmp_len = sizeof("Row 0123456789:\n") - 1;
                    title_buf->pos = ngx_pcalloc(r->pool, tmp_len);
                    if(!(title_buf->pos)) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "failed to allocate output buffer!");
                        ctx->cur_state = DB_ERR;
                        continue;
                    }
                    title_buf->last = ngx_snprintf(title_buf->pos, tmp_len,
                            "Row %L:\n", drizzle_row_current(&(ctx->dr_res)));
                    /* the buffer is newly allocated and its content can be changed freely */
                    title_buf->temporary = 1;

                    out_lst->buf = title_buf;
                    out_lst->next = 0;

                    for(i = 0; i < drizzle_result_column_count(&(ctx->dr_res)); ++i) {
                        ngx_buf_t *field_buf;
                        ngx_chain_t *new_lst = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));

                        if(!new_lst) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to allocate output chain node: row=%d, col=%d",
                                    drizzle_row_current(&(ctx->dr_res)), i);
                            ctx->cur_state = DB_ERR;
                            /* we will free all drizzle resources, so don't need to call drizzle_row_free() here */
                            continue;
                        }

                        field_buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
                        if(!field_buf) {
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                    "failed to allocate output buffer node: row=%d, col=%d",
                                    drizzle_row_current(&(ctx->dr_res)), i);
                            ctx->cur_state = DB_ERR;
                            continue;
                        }

                        if(!row[i]) {
                            tmp_len = sizeof("\t(NULL)\n") - 1;
                            field_buf->pos = ngx_pcalloc(r->pool, tmp_len);
                            if(!(field_buf->pos)) {
                                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                        "failed to allocate output buffer: row=%d, col=%d",
                                        drizzle_row_current(&(ctx->dr_res)), i);
                                ctx->cur_state = DB_ERR;
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
                                        drizzle_row_current(&(ctx->dr_res)), i);
                                ctx->cur_state = DB_ERR;
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

                DD("Row %llu:", drizzle_row_current(&(ctx->dr_res)));
                {
                    size_t *field_sizes;

                                        field_sizes = drizzle_row_field_sizes(&(ctx->dr_res));
                    uint16_t i;
                    for(i = 0; i < drizzle_result_column_count(&(ctx->dr_res)); ++i) {
                        if(!row[i]) {
                            DD("\t(NULL)");
                        } else {
                            DD("\t(%u) %*s", field_sizes[i], field_sizes[i], row[i]);
                        }
                    }
                }

                drizzle_row_free(&(ctx->dr_res), row);

                /* continue reading rows */
                continue;

            case DB_FIN:
            case DB_ERR:
                /* remove db connection from event pool */
                if(ngx_del_conn(ctx->ngx_db_con, NGX_CLOSE_EVENT) != NGX_OK) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to remove database connection from nginx event pool!");
                }
                ngx_free_connection(ctx->ngx_db_con);

                if(ctx->cur_state == DB_ERR) {
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

                /* manually finalize request only when the entering state is DB_INIT, */
                /* cause that call is not from our registered event handler but NginX */
                /* content phase handler, and it will finalize request after calling us. */
                if(old_state != DB_INIT) {
                    /* finalize current request manually  */
                    ngx_http_finalize_request(r, (ctx->cur_state == DB_FIN) ? NGX_HTTP_OK : NGX_HTTP_INTERNAL_SERVER_ERROR);
                }

                return (ctx->cur_state == DB_FIN) ? NGX_OK : NGX_ERROR;

            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "invalid mod_drizzle processing state: cur_state=%d", ctx->cur_state);
                return NGX_ERROR;
        }

        break;
    }

    return NGX_DONE;
}

