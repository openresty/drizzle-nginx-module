# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

repeat_each(100);

plan tests => repeat_each() * blocks();

worker_connections(1024);
run_tests();

no_diff();

__DATA__

=== TEST 1: bad query
little-endian systems only

--- http_config
    upstream foo {
        drizzle_server 127.0.0.1:3306 dbname=test
             password=some_pass user=monty protocol=mysql;
        drizzle_keepalive mode=single max=2 overflow=reject;
    }
--- config
    location /mysql {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query "update table_that_doesnt_exist set name='bob'";
    }
--- request
GET /mysql
--- error_code: 500



=== TEST 2: wrong credentials
little-endian systems only

--- http_config
    upstream foo {
        drizzle_server 127.0.0.1:3306 dbname=test
             password=wrong_pass user=monty protocol=mysql;
        drizzle_keepalive mode=single max=2 overflow=reject;
    }
--- config
    location /mysql {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query "update cats set name='bob' where name='bob'";
    }
--- request
GET /mysql
--- error_code: 502



=== TEST 3: no database
little-endian systems only

--- http_config
    upstream foo {
        drizzle_server 127.0.0.1:1 dbname=test
             password=some_pass user=monty protocol=mysql;
        drizzle_keepalive mode=single max=2 overflow=reject;
    }
--- config
    location /mysql {
        set $backend foo;
        drizzle_pass $backend;
        drizzle_module_header off;
        drizzle_query "update cats set name='bob' where name='bob'";
    }
--- request
GET /mysql
--- error_code: 502