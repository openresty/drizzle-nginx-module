# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

plan tests => repeat_each() * 2 * blocks();

run_tests();

no_diff();

__DATA__

=== TEST 1: sanity
little-endian systems only

db init:

create table cats (id integer, name text);
insert into cats (id) values (2);
insert into cats (id, name) values (3, 'bob');

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
"\x{00}". # endian
"\x{01}\x{00}\x{00}\x{00}". # format version 0.0.1
"\x{00}". # result type
"\x{00}\x{00}".  # std errcode
"\x{00}\x{00}" . # driver errcode
"\x{00}\x{00}".  # driver errstr len
"".  # driver errstr data
"\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}".  # rows affected
"\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}".  # insert id
"\x{02}\x{00}".  # col count
"\x{01}\x{00}".  # std col type (bigint/int)
"\x{03}\x{00}".  # drizzle col type
"\x{02}\x{00}".     # col name len
"id".   # col name data
"\x{13}\x{80}".  # std col type (blob/str)
"\x{fc}\x{00}".  # drizzle col type
"\x{04}\x{00}".  # col name len
"name".  # col name data
"\x{00}\x{00}".  # col list terminator
"\x{01}".  # valid row flag
"\x{01}\x{00}\x{00}\x{00}".  # field len
"2".  # field data
"\x{00}\x{00}\x{00}\x{00}".  # field len
"".  # field data
"\x{01}".  # valid row flag
"\x{01}\x{00}\x{00}\x{00}".  # field len
"3".  # field data
"\x{03}\x{00}\x{00}\x{00}".  # field len
"bob".  # field data
"\x{00}"  # row list terminator

