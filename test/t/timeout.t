# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

repeat_each(2);

plan tests => repeat_each() * blocks() * 2;

our $http_config = <<'_EOC_';
    upstream foo {
        drizzle_server www.google.com.hk:1234;
    }
_EOC_

worker_connections(128);
run_tests();

no_diff();

__DATA__

=== TEST 1: loc_config connect timeout
--- http_config eval: $::http_config
--- config
    location /upstream {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query 'select * from xx';
        drizzle_connect_timeout 3;
    }
--- request
GET /upstream
--- error_code: 504
--- response_body_like: 504 Gateway Time-out
--- timeout: 5



=== TEST2: http_config connect timeout
--- http_config eval: $::http_config
--- config
    drizzle_connect_timeout 6;
    location /upstream {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query 'select * from xx';
        drizzle_connect_timeout 3;
    }
--- request
GET /upstream
--- error_code: 504
--- response_body_like: 504 Gateway Time-out
--- timeout: 5



=== TEST3: serv_config connect timeout
--- http_config eval: $::http_config
--- config
    drizzle_connect_timeout 3;
    location /upstream {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query 'select * from xx';
    }
--- request
GET /upstream
--- error_code: 504
--- response_body_like: 504 Gateway Time-out
--- timeout: 5
