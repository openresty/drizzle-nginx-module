#!/bin/bash

# this file is mostly meant to be used by the author himself.

version=${1-"0.8.24"}
root=`readlink -f ..`
target=$root/work

# NOTE: changing cwd!
mkdir -p $root/{build,work}
cd $root/build

# mirror nginx source
lwp-mirror "http://sysoev.ru/nginx/nginx-$version.tar.gz" nginx-$version.tar.gz

# extracting source
tar -xzvf nginx-$version.tar.gz
cd nginx-$version/

# configuring
if [[ "$BUILD_CLEAN" -eq 1 || ! -f Makefile || "$root/config" -nt Makefile ]]; then
	rm -rf Makefile objs
    ./configure --prefix=$target \
          --with-http_addition_module \
          --add-module=$root \
          --with-debug \
		  --with-cc-opt="-g3 -O0"
fi
if [ -f $target/sbin/nginx ]; then
    rm -f $target/sbin/nginx
fi
if [ -f $target/logs/nginx.pid ]; then
    kill `cat $target/logs/nginx.pid`
fi

# make
make
make install

