# vi:filetype=

use Test::Nginx::Socket;

repeat_each(1);
plan tests => 58;
no_shuffle();
check_accum_error_log();
log_level('debug');

$ENV{TEST_NGINX_MYSQL_PORT} ||= 3306;
$ENV{TEST_NGINX_MYSQL_HOST} ||= '127.0.0.1';

our $backend = <<'_EOC_';
    upstream backend {
        drizzle_server $TEST_NGINX_MYSQL_HOST:$TEST_NGINX_MYSQL_PORT
                       protocol=mysql dbname=ngx_test
                       user=ngx_test password=ngx_test;
_EOC_

run_tests();

__DATA__

=== TEST 1: reject an invalid six-character overflow
--- http_config eval: $::backend . "drizzle_keepalive max=1 overflow=rejecx; }"
--- config
    location /t { return 200; }
--- must_die: 1
--- error_log
invalid parameter "overflow=rejecx"



=== TEST 2: reject an invalid six-character mode
--- http_config eval: $::backend . "drizzle_keepalive max=1 mode=singlx; }"
--- config
    location /t { return 200; }
--- must_die: 1
--- error_log
invalid parameter "mode=singlx"



=== TEST 3: reject an invalid five-character mode
--- http_config eval: $::backend . "drizzle_keepalive max=1 mode=multx; }"
--- config
    location /t { return 200; }
--- must_die: 1
--- error_log
invalid parameter "mode=multx"



=== TEST 4: reject an invalid five-character protocol
--- http_config
    upstream backend { drizzle_server 127.0.0.1:3306 protocol=mysqx; }
--- config
    location /t { return 200; }
--- must_die: 1
--- error_log
invalid protocol "mysqx"



=== TEST 5: reject an invalid seven-character protocol
--- http_config
    upstream backend { drizzle_server 127.0.0.1:3306 protocol=drizzlx; }
--- config
    location /t { return 200; }
--- must_die: 1
--- error_log
invalid protocol "drizzlx"



=== TEST 6: zero pool capacity disables overflow rejection
--- http_config eval: $::backend . "drizzle_keepalive max=0 overflow=reject; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
        rds_json on;
    }
--- request
GET /t
--- response_body: [{"answer":42}]
--- response_headers
X-Resty-DBD-Module: ngx_drizzle 0.1.14
--- no_error_log
[error]
[alert]
drizzle rejecting
password ngx_test
pass "ngx_test"



=== TEST 7: valid ignore and multi enums
--- http_config eval: $::backend . "drizzle_keepalive max=1 overflow=ignore mode=multi; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
        rds_json on;
    }
--- request
GET /t
--- response_body: [{"answer":42}]
--- no_error_log
[error]
[alert]



=== TEST 8: retry a failed connection before sending SQL
--- http_config eval
    "upstream backend { drizzle_server 127.0.0.1:1 protocol=mysql;" .
    "drizzle_server $ENV{TEST_NGINX_MYSQL_HOST}:$ENV{TEST_NGINX_MYSQL_PORT} " .
    "protocol=mysql dbname=ngx_test user=ngx_test password=ngx_test; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
        rds_json on;
    }
--- request
GET /t
--- response_body: [{"answer":42}]
--- error_log
connect() failed
--- no_error_log
[alert]



=== TEST 9: exhausting peers returns 502
--- http_config
    upstream backend {
        drizzle_server 127.0.0.1:1 protocol=mysql;
        drizzle_server 127.0.0.1:2 protocol=mysql;
    }
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
    }
--- request
GET /t
--- error_code: 502
--- no_error_log
[alert]



=== TEST 10: never retry after submitting SQL
--- http_config eval: $::backend . "drizzle_server 127.0.0.1:1 protocol=mysql; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select sleep(0.2)";
        drizzle_send_query_timeout 30ms;
    }
--- request
GET /t
--- error_code: 504
--- no_error_log
[alert]
port 1,



=== TEST 11: pause and resume while streaming a result larger than the threshold
--- http_config eval: $::backend . "drizzle_keepalive max=1; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select repeat('x', 131072) as payload";
        drizzle_buffer_size 4k;
        drizzle_max_buffered 4k;
        drizzle_recv_rows_timeout 30ms;
        limit_rate 64k;
        rds_json on;
    }
--- request
GET /t
--- response_body eval: '[{"payload":"' . ('x' x 131072) . '"}]'
--- timeout: 10
--- error_log
drizzle paused database reads for client output
drizzle resumed database reads after client output
--- no_error_log
[error]
[alert]
upstream timed out



=== TEST 12: omitted mode retains multi behavior across peers
--- http_config eval: $::backend . "drizzle_server 127.0.0.1:1 protocol=mysql; drizzle_keepalive max=1; }"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
        rds_json on;
    }
--- pipelined_requests eval: ["GET /t", "GET /t"]
--- response_body eval: ['[{"answer":42}]', '[{"answer":42}]']
--- error_log
connect() failed
--- no_error_log
[alert]



=== TEST 13: buffer threshold smaller than the buffer is rejected
--- config
    drizzle_buffer_size 8k;
    drizzle_max_buffered 4k;
    location /t { return 200; }
--- must_die: 1
--- error_log
drizzle_max_buffered must be at least drizzle_buffer_size



=== TEST 14: valid Drizzle protocol is accepted
--- http_config
    upstream backend { drizzle_server 127.0.0.1:1 protocol=drizzle; }
--- config
    location /t { return 200 "ok"; }
--- request
GET /t
--- response_body: ok
--- no_error_log
[emerg]



=== TEST 15: reuse output buffers for different atom sizes and multiple rows
--- http_config eval: $::backend . "}"
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select repeat('x', 256) as a, repeat('y', 3) as b union all select repeat('z', 2), repeat('w', 512)";
        drizzle_buffer_size 3;
        drizzle_max_buffered 32;
        rds_json on;
    }
--- request
GET /t
--- response_body eval: '[{"a":"' . ('x' x 256) . '","b":"yyy"},{"a":"zz","b":"' . ('w' x 512) . '"}]'
--- no_error_log
[error]
[alert]



=== TEST 16: debug connection logs never contain the configured password
--- http_config
    upstream backend {
        drizzle_server $TEST_NGINX_MYSQL_HOST:$TEST_NGINX_MYSQL_PORT
                       protocol=mysql dbname=ngx_test user=ngx_test
                       password=drizzle_log_password_sentinel;
    }
--- config
    location /t {
        drizzle_pass backend;
        drizzle_query "select 42 as answer";
    }
--- request
GET /t
--- error_code: 502
--- no_error_log
drizzle_log_password_sentinel
[alert]
