#! /bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

get_src()
{
    mkdir _build
    pushd _build

    curl -L https://github.com/nghttp2/nghttp2/releases/download/v$ng_ver/nghttp2-$ng_ver.tar.gz | tar xf -

    ls nghttp2-$ng_ver/lib/*.c >src.txt

    popd
}

build()
{
    pushd _build
    mkdir obj

    run="xcrun --sdk iphoneos"
    while read src; do
        echo $(basename $src)
        $run clang \
            -c $src -o obj/$(basename $src .c).o \
            -I nghttp2-$ng_ver/lib/includes \
            -target arm64-apple-ios15.0 \
            -O3 \
            -DNGHTTP2_STATICLIB \
            -DBUILDING_NGHTTP2 \
            -DHAVE_ARPA_INET_H=1
    done <src.txt

    echo libnghttp2.a
    $run ar cr libnghttp2.a obj/*.o

    popd
}

deploy()
{
	target_dir=../../lib/IOS/Release
	p4 open $target_dir/...
    mv -f _build/libnghttp2.a $target_dir
}

deploy_full()
{
    mkdir $ng_ver
    pushd $ng_ver

    mkdir lib
    mkdir -p include/nghttp2

    mv ../_build/libnghttp2.a lib
    find ../_build/nghttp2-$ng_ver/lib/includes/nghttp2 -name \*.h -exec mv {} include/nghttp2 \;

    popd
}

clean()
{
    rm -rf _build
}

cd $(dirname ${BASH_SOURCE[0]})

ng_ver=1.64.0

get_src
build
deploy
clean
