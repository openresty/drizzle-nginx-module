#ifndef MOD_DRIZZLE_COMMON_H__
#define MOD_DRIZZLE_COMMON_H__

#include <nginx.h>    /* for nginx version macro */
#include <ngx_http.h>
#include <libdrizzle/drizzle_client.h>
#include "ddebug.h"

/* Per-location configuration for module drizzle */
typedef struct {
    ngx_str_t db_host;    //< MySQL service hostname for current location, default to "localhost"
    ngx_int_t db_port;    //< MySQL service port for current location, default to 3306
    ngx_str_t db_user;    //< MySQL service user, default to "root"
    ngx_str_t db_pass;    //< MySQL service password, default to ""
    ngx_str_t db_name;    //< MySQL database name, default to "test"
    ngx_str_t raw_sql;    //< SQL statement to be executed, default to ""
    ngx_array_t *sql_lengths;    //< Holding NginX variable lengths in the raw SQL statement
    ngx_array_t *sql_values;    //< Holding NginX variable values in the raw SQL statement
} ngx_http_drizzle_loc_conf_t;

typedef enum {
    DB_INIT,
    DB_CONNECT,
    DB_SEND_QUERY,
    DB_RECV_FIELDS,
    DB_RECV_ROWS,
    DB_FIN,
    DB_ERR
} proc_state_t;

typedef struct {
    proc_state_t cur_state;
    ngx_str_t sql;
    ngx_connection_t *ngx_db_con;
    drizzle_st dr;
    drizzle_con_st dr_con;
    drizzle_result_st dr_res;
} ngx_http_drizzle_ctx_t;

#endif

