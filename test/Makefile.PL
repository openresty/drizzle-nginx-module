use strict;
use lib '.';
use inc::Module::Install;

name            ('Test-Nginx-Drizzle');
license         ('bsd');
perl_version    ('5.006001');
repository      ('http://github.com/chaoslawful/drizzle-nginx-module');
all_from        ('lib/Test/Nginx/Drizzle.pm');

use_test_base();
auto_install();

#tests('t/*.t t/*/*.t');

WriteAll();

