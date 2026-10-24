# vi:filetype=

use Test::Nginx::Socket;

if (!$ENV{TEST_NGINX_MYSQL_IPV6_HOST}) {
    plan skip_all => 'set TEST_NGINX_MYSQL_IPV6_HOST to an IPv6 MySQL server';
}

$ENV{TEST_NGINX_MYSQL_IPV6_PORT} ||= 3306;

repeat_each(2);
plan tests => repeat_each() * 4 * blocks();

run_tests();

__DATA__

=== TEST 1: multi keepalive preserves the complete IPv6 peer address
--- http_config
    upstream backend {
        drizzle_server [$TEST_NGINX_MYSQL_IPV6_HOST]:$TEST_NGINX_MYSQL_IPV6_PORT
                       protocol=mysql dbname=ngx_test
                       user=ngx_test password=ngx_test;
        drizzle_keepalive max=1 mode=multi overflow=reject;
    }
--- config
    location /query {
        drizzle_pass backend;
        drizzle_query "select connection_id() as id";
        rds_json on;
    }

    location /t {
        echo_location /query;
        echo_location /query;
    }
--- request
GET /t
--- response_body_like: ^\[\{"id":([0-9]+)\}\]\[\{"id":\1\}\]$
--- no_error_log
[error]
[alert]
