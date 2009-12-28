# vi:filetype=perl

use lib 'lib';
use Test::Nginx::LWP;

plan tests => $Test::Nginx::LWP::RepeatEach * 2 * blocks();

run_tests();

__DATA__

=== TEST 1: sanity
--- http_config
    upstream backend {
        drizzle_server 127.0.0.1:3306 dbname=test
             password=some_pass user=monty protocol=mysql;
    }
--- config
    location /mysql {
        drizzle_pass backend;
        #drizzle_dbname $dbname;
        drizzle_query 'select * from cats';
    }
--- request
GET /mysql
--- response_body

