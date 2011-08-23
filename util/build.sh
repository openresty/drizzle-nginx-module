#!/bin/bash

# this file is mostly meant to be used by the author himself.

root=`pwd`
version=$1
home=~
#opts=$2
target=$root/work/nginx

if [ ! -d ./buildroot ]; then
    mkdir ./buildroot || exit 1
fi

cd buildroot || exit 1

if [ ! -s "nginx-$version.tar.gz" ]; then
    if [ -f ~/work/nginx-$version.tar.gz ]; then
        cp ~/work/nginx-$version.tar.gz ./ || exit 1
    else
        wget "http://nginx.org/download/nginx-$version.tar.gz" -O nginx-$version.tar.gz || exit 1
        cp nginx-$version.tar.gz ~/work/
    fi

    tar -xzvf nginx-$version.tar.gz || exit 1
    cp $root/../no-pool-nginx/nginx-$version-no_pool.patch ./ || exit 1
    patch -p0 < nginx-$version-no_pool.patch || exit 1
fi

#tar -xzvf nginx-$version.tar.gz || exit 1
#cp $root/../no-pool-nginx/nginx-$version-no_pool.patch ./ || exit 1
#patch -p0 < nginx-$version-no_pool.patch || exit 1

cd nginx-$version/ || exit 1

if [[ "$BUILD_CLEAN" -eq 1 || ! -f Makefile || "$root/config" -nt Makefile || "$root/util/build.sh" -nt Makefile ]]; then
            #--with-cc-opt='-fast' \
    ./configure --prefix=$target \
            --with-ld-opt="-Wl,-rpath,/opt/drizzle/lib" \
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
          --add-module=$root/../rds-json-nginx-module \
          --add-module=$root/../headers-more-nginx-module \
          --add-module=$root $opts \
          --add-module=$root/../ndk-nginx-module \
          --add-module=$root/../set-misc-nginx-module \
          --with-debug
          #--with-cc-opt="-g3 -O0"
          #--add-module=$home/work/nginx_eval_module-1.0.1 \
          #--add-module=$root/../echo-nginx-module \
  #--without-http_ssi_module  # we cannot disable ssi because echo_location_async depends on it (i dunno why?!)

fi
if [ -f $target/sbin/nginx ]; then
    rm -f $target/sbin/nginx
fi
if [ -f $target/logs/nginx.pid ]; then
    kill `cat $target/logs/nginx.pid`
fi
make -j3
make install

