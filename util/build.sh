#!/bin/bash

# this file is mostly meant to be used by the author himself.

root=`pwd`
version=$1
home=~
force=$2


ngx-build $force $version \
            --with-ld-opt="-L$OPENSSL_LIB -L$PCRE_LIB -Wl,-rpath,$OPENSSL_LIB:$PCRE_LIB:$LIBDRIZZLE_LIB:/usr/local/lib" \
            --with-cc-opt="-DDEBUG_MALLOC -I $OPENSSL_INC -I $PCRE_INC" \
            --with-http_ssl_module \
            --without-mail_pop3_module \
            --without-mail_imap_module \
            --without-mail_smtp_module \
            --without-http_upstream_ip_hash_module \
            --without-http_empty_gif_module \
            --without-http_memcached_module \
            --without-http_referer_module \
            --without-http_autoindex_module \
            --without-http_auth_basic_module \
            --without-http_userid_module \
          --add-module=$root/../echo-nginx-module \
          --add-module=$root/../lua-nginx-module \
          --add-module=$root/../rds-json-nginx-module \
          --add-module=$root/../headers-more-nginx-module \
          --add-module=$root $opts \
          --add-module=$root/../ndk-nginx-module \
          --add-module=$root/../set-misc-nginx-module \
          --with-select_module \
          --with-poll_module \
          --with-debug

