# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

plan tests => repeat_each() * 2 * blocks();

run_tests();

no_diff();

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
--- response_body eval
"\x{00}\x{01}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{02}\x{00}\x{01}\x{00}\x{03}\x{00}\x{02}\x{00}id\x{13}\x{80}\x{fc}\x{00}\x{04}\x{00}name\x{00}\x{00}\x{01}\x{01}\x{00}"

