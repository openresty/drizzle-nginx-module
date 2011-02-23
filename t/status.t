# vi:filetype=

use lib 'lib';
use Test::Nginx::Socket;

#repeat_each(2);

plan tests => repeat_each() * 2 * blocks() + 2 * repeat_each() * 6;

$ENV{TEST_NGINX_MYSQL_PORT} ||= 3306;

our $http_config = <<'_EOC_';
    upstream backend {
        drizzle_server 127.0.0.1:$TEST_NGINX_MYSQL_PORT protocol=mysql
                       dbname=ngx_test user=ngx_test password=ngx_test;
        drizzle_keepalive max=10 overflow=reject mode=single;
    }
    upstream backend2 {
        drizzle_server 127.0.0.1:$TEST_NGINX_MYSQL_PORT protocol=mysql
                       dbname=ngx_test user=ngx_test password=ngx_test;
        #drizzle_keepalive max=10 overflow=ignore mode=single;
    }

_EOC_

worker_connections(128);

no_long_string();
#no_diff();

run_tests();

__DATA__

=== TEST 1: sanity
--- http_config eval: $::http_config
--- config
    location /status {
        drizzle_status;
    }
--- request
    GET /status
--- response_body
upstream backend
  active connections: 0
  max connections allowed: 10
  overflow: reject

upstream backend2
  active connections: 0
  max connections allowed: 0
  overflow: ignore
--- ONLY

