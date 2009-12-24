/* Copyright (C) agentzh */

#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_drizzle_module.h"
#include "ngx_http_upstream_drizzle.h"
#include "ngx_http_drizzle_processor.h"
#include "ngx_http_drizzle_util.h"

enum {
    ngx_http_drizzle_default_port = 3306
};

static ngx_int_t ngx_http_upstream_drizzle_init(ngx_conf_t *cf,
        ngx_http_upstream_srv_conf_t *uscf);

static ngx_int_t ngx_http_upstream_drizzle_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *uscf);

static ngx_int_t ngx_http_upstream_drizzle_get_peer(ngx_peer_connection_t *pc,
        void *data);

static void ngx_http_upstream_drizzle_free_peer(ngx_peer_connection_t *pc,
        void *data, ngx_uint_t state);

/* just a work-around to override the default u->output_filter */
static ngx_int_t ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in);


void *
ngx_http_upstream_drizzle_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_upstream_drizzle_srv_conf_t  *conf;

    conf = ngx_palloc(cf->pool,
                       sizeof(ngx_http_upstream_drizzle_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->peers   = NULL;
    conf->current = 0;
    conf->servers = NULL;

    (void) drizzle_create(&conf->drizzle);

    drizzle_add_options(&conf->drizzle, DRIZZLE_NON_BLOCKING);

    return conf;
}


/* mostly based on ngx_http_upstream_server in
 * ngx_http_upstream.c of nginx 0.8.30.
 * Copyright (C) Igor Sysoev */
char *
ngx_http_upstream_drizzle_server(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    ngx_http_upstream_drizzle_srv_conf_t        *dscf = conf;
    ngx_http_upstream_drizzle_server_t          *ds;
    ngx_str_t                                   *value;
    ngx_url_t                                    u;
    ngx_uint_t                                   i;
    ngx_http_upstream_srv_conf_t                *uscf;
    ngx_str_t                                    protocol;

    if (dscf->servers == NULL) {
        dscf->servers = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_http_upstream_drizzle_server_t));
        if (dscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    ds = ngx_array_push(dscf->servers);
    if (ds == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(ds, sizeof(ngx_http_upstream_drizzle_server_t));

    value = cf->args->elts;

    /* parse the first name:port argument */

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.default_port = 80;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "%s in drizzle upstream \"%V\"", u.err, &u.url);
        }

        return NGX_CONF_ERROR;
    }

    ds->addrs  = u.addrs;
    ds->naddrs = u.naddrs;
    ds->port   = u.port;
    ds->protocol = ngx_http_drizzle_protocol;

    /* parse various options */

    for (i = 2; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "dbname=", sizeof("dbname=") - 1)
                == 0)
        {
            ds->dbname.len = value[i].len - (sizeof("dbname=") - 1);
            if (ds->dbname.len >= DRIZZLE_MAX_DB_SIZE) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                       "\"dbname\" value too large in drizzle upstream \"%V\""
                       " (at most %d bytes)",
                       dscf->peers->name,
                       (int) DRIZZLE_MAX_DB_SIZE);

                return NGX_CONF_ERROR;
            }

            ds->dbname.data = &value[i].data[sizeof("dbname=") - 1];

            continue;
        }

        if (ngx_strncmp(value[i].data, "user=", sizeof("user=") - 1)
                == 0)
        {
            ds->user.len = value[i].len - (sizeof("user=") - 1);

            if (ds->user.len >= DRIZZLE_MAX_USER_SIZE) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                       "\"user\" value too large in drizzle upstream \"%V\""
                       " (at most %d bytes)",
                       dscf->peers->name,
                       (int) DRIZZLE_MAX_USER_SIZE);

                return NGX_CONF_ERROR;
            }

            ds->user.data = &value[i].data[sizeof("user=") - 1];

            continue;
        }

        if (ngx_strncmp(value[i].data, "password=", sizeof("password=") - 1)
                == 0)
        {
            ds->password.len = value[i].len - (sizeof("password=") - 1);

            if (ds->password.len >= DRIZZLE_MAX_PASSWORD_SIZE) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                       "\"password\" value too large in drizzle upstream \"%V\""
                       " (at most %d bytes)",
                       dscf->peers->name,
                       (int) DRIZZLE_MAX_PASSWORD_SIZE);

                return NGX_CONF_ERROR;
            }

            ds->password.data = &value[i].data[sizeof("password=") - 1];

            continue;
        }

        if (ngx_strncmp(value[i].data, "protocol=", sizeof("protocol=") - 1)
                == 0)
        {
            protocol.len = value[i].len - (sizeof("protocol=") - 1);
            protocol.data = &value[i].data[sizeof("protocol=") - 1];

            switch (protocol.len) {
            case 5:
                if (ngx_str5cmp(protocol.data, 'm', 'y', 's', 'q', 'l')) {
                    ds->protocol = ngx_http_mysql_protocol;
                } else {
                    continue;
                }

                break;

            case 7:
                if ( ! ngx_str7cmp(protocol.data,
                            'd', 'r', 'i', 'z', 'z', 'l', 'e'))
                {
                    continue;
                }

                break;
            default:
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid protocol \"%V\"", &protocol);

                return NGX_CONF_ERROR;
            }

            continue;
        }


        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);

        return NGX_CONF_ERROR;
    }

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    uscf->peer.init_upstream = ngx_http_upstream_drizzle_init;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_upstream_drizzle_init(ngx_conf_t *cf,
        ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_uint_t                               i, j, n;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_drizzle_server_t      *server;
    ngx_http_upstream_drizzle_peers_t       *peers;
    size_t                                   len;

    uscf->peer.init = ngx_http_upstream_drizzle_init_peer;

    dscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_drizzle_module);

    if (dscf->servers) {
        server = uscf->servers->elts;

        n = 0;

        for (i = 0; i < uscf->servers->nelts; i++) {
            n += server[i].naddrs;
        }

        peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_drizzle_peers_t)
                + sizeof(ngx_http_upstream_drizzle_peer_t) * (n - 1));

        if (peers == NULL) {
            return NGX_ERROR;
        }

        peers->single = (n == 1);
        peers->number = n;
        peers->name = &uscf->host;

        n = 0;

        for (i = 0; i < uscf->servers->nelts; i++) {
            for (j = 0; j < server[i].naddrs; j++) {
                peers->peer[n].sockaddr = server[i].addrs[j].sockaddr;
                peers->peer[n].socklen = server[i].addrs[j].socklen;
                peers->peer[n].name = server[i].addrs[j].name;
                peers->peer[n].port = server[i].port;
                peers->peer[n].user = server[i].user;
                peers->peer[n].password = server[i].password;
                peers->peer[n].dbname = server[i].dbname;
                peers->peer[n].protocol = server[i].protocol;

                len = sizeof("255.255.255.255");
                peers->peer[n].host = ngx_palloc(cf->pool, len);

                len = ngx_sock_ntop(peers->peer[n].sockaddr,
                        peers->peer[n].host,
                        len - 1, 0 /* no port */);

                peers->peer[n].host[len] = '\0';

                n++;
            }
        }

        dscf->peers = peers;

        return NGX_OK;
    }

    /* XXX an upstream implicitly defined by drizzle_pass, etc.,
     * is not allowed for now */

    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                  "no drizzle_server defined in upstream \"%V\" in %s:%ui",
                  &uscf->host, uscf->file_name, uscf->line);

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_drizzle_init_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_http_upstream_drizzle_peer_data_t   *dp;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_t                     *u;
    ngx_http_drizzle_loc_conf_t             *dlcf;
    ngx_str_t                                dbname;
    ngx_str_t                                query;

    dp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_drizzle_peer_data_t));
    if (dp == NULL) {
        return NGX_ERROR;
    }

    u = r->upstream;

    dscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_drizzle_module);

    dp->conf     = dscf;
    dp->upstream = u;
    dp->request  = r;

    dp->query.len  = 0;
    dp->dbname.len = 0;
    dp->state = state_db_init;

    /* to force ngx_output_chain not to use ngx_chain_writer */

    u->output.output_filter = (ngx_event_pipe_output_filter_pt)
                                ngx_http_drizzle_output_filter;
    u->output.filter_ctx = NULL;
    u->output.in   = NULL;
    u->output.busy = NULL;

    u->peer.data = dp;
    u->peer.get = ngx_http_upstream_drizzle_get_peer;
    u->peer.free = ngx_http_upstream_drizzle_free_peer;

    dlcf = ngx_http_get_module_loc_conf(r, ngx_http_drizzle_module);

    /* prepare dbname */

    dp->dbname.len = 0;

    if (dlcf->dbname) {
        /* check if dbname requires overriding at request time */
        if (ngx_http_complex_value(r, dlcf->dbname, &dbname) != NGX_OK) {
            return NGX_ERROR;
        }

        if (dbname.len) {
            if (dbname.len >= DRIZZLE_MAX_DB_SIZE) {
                ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                       "\"dbname\" value too large in drizzle upstream \"%V\"",
                       dscf->peers->name);

                return NGX_ERROR;
            }

            dp->dbname = dbname;
        }
    }

    /* prepare SQL query */

    if (dlcf->query == NULL) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                       "empty \"query\" in drizzle upstream \"%V\"",
                       dscf->peers->name);

        goto empty_query;
    }

    if (ngx_http_complex_value(r, dlcf->query, &query) != NGX_OK) {
        return NGX_ERROR;
    }

    if (query.len == 0) {
        goto empty_query;
    }

    dp->query = query;

    return NGX_OK;

empty_query:

    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                   "empty \"query\" in drizzle upstream \"%V\"",
                   dscf->peers->name);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_upstream_drizzle_get_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_drizzle_peer_data_t   *dp = data;
    ngx_http_upstream_drizzle_srv_conf_t    *dscf;
    ngx_http_upstream_drizzle_peers_t       *peers;
    ngx_http_upstream_drizzle_peer_t        *peer;
    ngx_http_request_t                      *r;
    drizzle_con_st                          *con;
    ngx_str_t                                dbname;
    drizzle_return_t                         ret;
    int                                      fd;

    dscf = dp->conf;

    peers = dscf->peers;

    if (dscf->current >= peers->number) {
        dscf->current = 0;
    }

    peer = &peers->peer[dscf->current++];

    r = dp->request;

    /* set up the peer's drizzle connection */

    con = &dp->drizzle_con;

    (void) drizzle_con_create(&dscf->drizzle, con);

    /* set protocol for the drizzle connection */

    if (peer->protocol == ngx_http_mysql_protocol) {
        drizzle_con_add_options(con, DRIZZLE_CON_MYSQL);
    }

    /* set dbname for the drizzle connection */

    if (dp->dbname.len) {
        dbname = dp->dbname;
    } else {
        dbname = peer->dbname;
    }

    ngx_memcpy(con->db, dbname.data, dbname.len);
    con->db[dbname.len] = '\0';

    /* set user for the drizzle connection */

    ngx_memcpy(con->user, peer->user.data, peer->user.len);
    con->user[peer->user.len] = '\0';

    /* set password for the drizzle connection */

    ngx_memcpy(con->password, peer->password.data, peer->password.len);
    con->password[peer->password.len] = '\0';

    /* set host and port for the drizzle connection */

    drizzle_con_set_tcp(con, (char *) peer->host, peer->port);

    ret = drizzle_con_connect(con);

    if (ret != DRIZZLE_RETURN_OK && ret != DRIZZLE_RETURN_IO_WAIT) {
       ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                       "failed to connect: %d: %s in drizzle upstream \"%V\"",
                       (int) ret,
                       drizzle_error(&dscf->drizzle),
                       &peer->name);

        return NGX_ERROR;
    }

    /* register the file descriptor (fd) into the nginx event model */

    fd = drizzle_con_fd(con);

    if (fd == -1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "failed to get the drizzle connection fd");

        goto invalid;
    }

    dp->nginx_con = ngx_get_connection(fd, r->connection->log);

    if (dp->nginx_con == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "failed to get a free nginx connection");

        goto invalid;
    }

    dp->nginx_con->log_error = r->connection->log_error;
    dp->nginx_con->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    dp->nginx_con->data = r;
    dp->nginx_con->log = r->connection->log;

    dp->nginx_con->read->log = r->connection->log;
    dp->nginx_con->read->handler = ngx_http_drizzle_event_handler;
    dp->nginx_con->read->data = dp->nginx_con;

    dp->nginx_con->write->log = r->connection->log;
    dp->nginx_con->write->handler = ngx_http_drizzle_event_handler;
    dp->nginx_con->write->data = dp->nginx_con;

    if (ngx_add_conn == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "no ngx_add_conn found in the nginx core");

        goto invalid;
    }

    if (ngx_add_conn(dp->nginx_con) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "failed to add connection into nginx event model");

        goto invalid;
    }

    if (ret == DRIZZLE_RETURN_OK) {
        dp->state = state_db_send_query;

        return NGX_DONE;
    }

    /* ret == DRIZZLE_RETURN_IO_WAIT */

    dp->state = state_db_connect;

    return NGX_AGAIN;

invalid:

    drizzle_con_close(con);

    return NGX_ERROR;
}


static void
ngx_http_upstream_drizzle_free_peer(ngx_peer_connection_t *pc,
        void *data, ngx_uint_t state)
{
}


static ngx_int_t
ngx_http_drizzle_output_filter(ngx_http_request_t *r,
        ngx_chain_t *in)
{
    return ngx_http_drizzle_process_events(r);
}

